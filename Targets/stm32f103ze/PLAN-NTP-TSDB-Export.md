# Plan: NTP Time Sync + Epoch TSDB + Auto Export + LCD History

## Context

系統目前無 RTC，TSDB 時間戳用 FreeRTOS tick (ms since boot)，重啟後歸零。
需要：
1. **NTP 校時** — ESP8266 SNTP → 取得 UTC+8 epoch → 軟體時鐘
2. **LCD 顯示時間** — 標題列顯示 `HH:MM:SS`
3. **TSDB epoch 時間戳** — `fdbGetTime()` 回傳 epoch seconds，支援日期查詢
4. **午夜自動匯出** — 偵測日期切換 → `exportDailyFile(yesterday)` → `.enc` 檔
5. **TSDB 生命週期** — rollover 循環緩衝區，自動覆蓋最舊資料
6. **LCD 歷史瀏覽** — KEY1/KEY2 選日期 → 查 TSDB 或 .enc → 顯示摘要

## Architecture

```
NTP 校時:
  WiFi connected → AT+CIPSNTPCFG=1,8,"time.stdtime.gov.tw"
    → AT+CIPSNTPTIME? → parse "Thu Mar 10 14:30:00 2026"
    → SystemClock::sync(epoch) — 存 epochAtSync + tickAtSync
    → 每 6 小時自動重新校時

時間流向:
  SystemClock::now() → epoch seconds
    → LCD 每秒更新 HH:MM:SS (subscribe BaseTimer)
    → fdbGetTime() 回傳 epoch seconds
    → TSDB records 帶真實時間戳
    → fdb_tsl_iter_by_time(dayStart, dayEnd) 日期查詢

午夜自動匯出:
  taskLoop 每秒檢查 → dayChanged?
    → exportDailyFile(yesterday) 用 fdb_tsl_iter_by_time
    → 寫 YYYYMMDD.enc 到 SD 卡根目錄
    → TSDB 記錄不刪除，rollover 自動管理空間

LCD 歷史瀏覽:
  KEY1(←) / KEY2(→) 切換日期
    → TSDB 有資料? → fdb_tsl_iter_by_time 查詢解密
    → TSDB 無資料? → 讀 YYYYMMDD.enc 檔案解密
    → 顯示: 日期、筆數、溫度 min/max/avg
```

## TSDB 生命週期設計

### 答覆使用者問題

**Q: TSDB 每日匯出後會自動清空嗎？**
不會。FlashDB `rollover=true` (預設) 為循環緩衝區：
- 寫滿時自動覆蓋最舊 sector，無需手動清空
- 匯出後記錄仍在 TSDB，直到被新資料自然覆蓋
- 用 `fdb_tsl_set_status(FDB_TSL_USER_STATUS1)` 標記已匯出（可選）

**Q: 當天需要中途上傳怎麼辦？**
- `.enc` 檔案是快照，可隨時重新匯出
- 中途上傳：`exportDailyFile(today)` → 匯出目前為止的記錄
- 晚上再匯出：覆蓋同一個 `.enc`，包含完整一天的記錄
- KVDB 上傳追蹤以 `.enc` 檔案為單位，非個別記錄

**Q: LCD 可以查到過去幾天的記錄嗎？**
可以，兩層查詢：
1. **TSDB 查詢** (近期)：`fdb_tsl_iter_by_time()` 查最近 N 天
2. **`.enc` 檔案** (更早)：若 TSDB 已 rollover 覆蓋，從 SD 卡讀 `.enc` 解密
3. TSDB 2MB → 1 rec/s → ~21 小時 ≈ 當天完整 + 部分昨天

### TSDB Partition Sizing

| 設定 | 值 | 說明 |
|------|-----|------|
| TSDB_SIZE | 2 MB | 512 sectors × 4KB |
| 容量 | ~60K records | 26 bytes + ~7 bytes overhead/record |
| 1 rec/s 保留 | ~16.7 小時 | 滿後 rollover 覆蓋最舊 |
| 1 rec/10s 保留 | ~7 天 | 低頻率可保留更久 |

## Implementation Steps

### Step 1: SystemClock (header-only singleton)

**New file**: `Services/common/SystemClock.hpp`

```cpp
#pragma once
#include "FreeRTOS.h"
#include "task.h"
#include <cstdint>

namespace arcana {

class SystemClock {
public:
    static SystemClock& getInstance() {
        static SystemClock sInstance;
        return sInstance;
    }

    void sync(uint32_t epochSec) {
        mEpochAtSync = epochSec;
        mTickAtSync = xTaskGetTickCount();
        mSynced = true;
    }

    bool isSynced() const { return mSynced; }

    uint32_t now() const {
        if (!mSynced) return 0;
        uint32_t elapsed = (xTaskGetTickCount() - mTickAtSync) / configTICK_RATE_HZ;
        return mEpochAtSync + elapsed;
    }

    // Date helpers (UTC+8 already baked into epoch from SNTP)
    static uint32_t startOfDay(uint32_t epoch) {
        return epoch - (epoch % 86400);
    }
    static uint32_t dateYYYYMMDD(uint32_t epoch); // epoch → 20260310
    static void toHMS(uint32_t epoch, uint8_t& h, uint8_t& m, uint8_t& s);

private:
    SystemClock() : mEpochAtSync(0), mTickAtSync(0), mSynced(false) {}
    uint32_t mEpochAtSync;
    TickType_t mTickAtSync;
    bool mSynced;
};

} // namespace arcana
```

Note: `dateYYYYMMDD()` and `toHMS()` need civil date conversion (days since epoch → year/month/day). Implement using standard algorithm (no dependency on `<time.h>` to avoid newlib bloat).

**Build**: Header-only, no subdir.mk change. `-I../Services/common` already in include paths.

### Step 2: NTP Sync in WifiMqttServiceImpl

**Modify**: `Services/service/impl/WifiMqttServiceImpl.cpp`

After `connectWifi()` succeeds in `runTask()`, add NTP sync:

```cpp
// In runTask(), after connectWifi() returns true:
syncNtp();

// New private method:
bool WifiMqttServiceImpl::syncNtp() {
    // Configure SNTP: timezone=+8, server=time.stdtime.gov.tw
    mEsp.sendCmd("AT+CIPSNTPCFG=1,8,\"time.stdtime.gov.tw\"", "OK", 3000);
    vTaskDelay(pdMS_TO_TICKS(3000)); // Wait for NTP response

    // Query time (retry up to 5 times)
    for (int i = 0; i < 5; i++) {
        if (mEsp.sendCmd("AT+CIPSNTPTIME?", "OK", 3000)) {
            // Response: "+CIPSNTPTIME:Thu Mar 10 14:30:00 2026"
            uint32_t epoch = parseNtpResponse(mEsp.getResponse());
            if (epoch > 1700000000) { // sanity: after 2023
                SystemClock::getInstance().sync(epoch);
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return false;
}
```

**Parse AT+CIPSNTPTIME?** response format: `Thu Mar 10 14:30:00 2026`
- Parse month name → month number, day, hour, minute, second, year
- Convert to epoch using civil date → days → seconds formula
- No `mktime()` — implement minimal conversion (avoid newlib dependency)

**Add to WifiMqttServiceImpl.hpp**: `bool syncNtp()`, `uint32_t parseNtpResponse(const char*)`, `void resyncIfNeeded()`

**Periodic resync**: In main publish loop, check every ~6 hours → call `syncNtp()` again.

### Step 3: LCD Time Display

**Modify**: `Services/service/impl/LcdServiceImpl.cpp`

Subscribe to `TimerService::BaseTimer` (1-second events). On each tick:

```cpp
// In onBaseTimer callback:
if (SystemClock::getInstance().isSynced()) {
    uint8_t h, m, s;
    SystemClock::toHMS(SystemClock::getInstance().now(), h, m, s);
    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u:%02u", h, m, s);
    // Draw at top-right of title bar (Y=4, X=170)
    disp.fillRect(170, 4, 60, 10, 0x0000);
    disp.drawString(170, 4, timeBuf, 0xFFFF, 0x0000, 1);
}
```

**Modify** `LcdService.hpp` interface: add `Observable<TimerModel>* BaseTimer` to input struct.

**Modify** `Controller.cpp` wiring: `mLcd->input.BaseTimer = mTimer->output.BaseTimer;`

### Step 4: TSDB Epoch Timestamps

**Modify**: `Services/service/impl/SdStorageServiceImpl.cpp`

Update `fdbGetTime()` to use SystemClock when synced:

```cpp
static fdb_time_t fdbGetTime(void) {
    fdb_time_t now;
    if (SystemClock::getInstance().isSynced()) {
        now = (fdb_time_t)SystemClock::getInstance().now(); // epoch seconds
    } else {
        now = (fdb_time_t)xTaskGetTickCount(); // fallback: tick ms
    }
    if (now <= sLastTime) {
        now = sLastTime + 1;
    }
    sLastTime = now;
    return now;
}
```

**Transition handling**: After NTP sync, timestamps jump from tick (small) to epoch (large ~1.7B). This is fine because monotonicity is preserved (epoch >> tick).

### Step 5: Midnight Auto-Export

**Modify**: `Services/service/impl/SdStorageServiceImpl.cpp` — `taskLoop()`

```cpp
void SdStorageServiceImpl::taskLoop() {
    uint32_t lastDay = 0; // Track current day for change detection

    while (mRunning) {
        if (xSemaphoreTake(mWriteSem, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!mRunning) break;
            appendRecord(&mPendingData);
        }

        // Check for day change (only when clock is synced)
        if (SystemClock::getInstance().isSynced()) {
            uint32_t today = SystemClock::dateYYYYMMDD(
                SystemClock::getInstance().now());
            if (lastDay != 0 && today != lastDay) {
                // Day changed! Export yesterday's data
                exportDailyFile(lastDay);
            }
            lastDay = today;
        }
    }
}
```

**Update `exportDailyFile()`**: Use `fdb_tsl_iter_by_time()` instead of `fdb_tsl_iter()`:

```cpp
bool SdStorageServiceImpl::exportDailyFile(uint32_t dateYYYYMMDD) {
    // Convert YYYYMMDD → epoch range [dayStart, dayEnd)
    uint32_t dayStart = dateToEpoch(dateYYYYMMDD);
    uint32_t dayEnd = dayStart + 86400;

    // ... open file, write header placeholder ...

    // Time-filtered iteration
    fdb_tsl_iter_by_time(&mTsdb, dayStart, dayEnd - 1, exportIterCb, &ctx);

    // ... update header, close file ...
}
```

Add helper `dateToEpoch(uint32_t YYYYMMDD) → uint32_t epoch`.

### Step 6: Increase TSDB Partition Size

**Modify**: `Services/driver/SdFalAdapter.hpp`

```cpp
static const uint32_t TSDB_SIZE = 2 * 1024 * 1024;  // 2 MB (512 sectors)
```

This gives ~60K records, ~16.7 hours at 1 rec/s.

### Step 7: LCD History Browsing (Phase 2 — can be deferred)

**New model**: Add `ButtonEventModel` to `F103Models.hpp`:
```cpp
enum class F103ModelType : uint8_t {
    // ... existing ...
    ButtonEvent = 107
};
class ButtonEventModel : public Model {
public:
    uint8_t buttonId; // 1=KEY1, 2=KEY2
    ButtonEventModel() : Model(static_cast<uint8_t>(F103ModelType::ButtonEvent)), buttonId(0) {}
};
```

**New service**: `Services/service/impl/ButtonServiceImpl.hpp/.cpp`
- Poll KEY1/KEY2 every 50ms (via TimerService FastTimer subscription)
- Debounce: require 3 consecutive reads (150ms)
- Publish `ButtonEventModel` on press

**Modify LcdServiceImpl**: Add date browsing mode
- Subscribe to `ButtonEventModel`
- KEY1 = previous day, KEY2 = next day
- Query SdStorageService for selected date
- Display: date, record count, temp min/max/avg
- Auto-return to live mode after 10s inactivity

## Files to Modify

| File | Change |
|------|--------|
| `Services/common/SystemClock.hpp` | **NEW** — header-only singleton |
| `Services/service/impl/WifiMqttServiceImpl.hpp` | Add `syncNtp()`, `parseNtpResponse()` |
| `Services/service/impl/WifiMqttServiceImpl.cpp` | Add NTP sync after WiFi connect |
| `Services/service/impl/SdStorageServiceImpl.cpp` | epoch `fdbGetTime()`, midnight export, time-filtered iter |
| `Services/service/impl/SdStorageServiceImpl.hpp` | Add `dateToEpoch()` helper |
| `Services/service/impl/LcdServiceImpl.hpp` | Add BaseTimer input |
| `Services/service/impl/LcdServiceImpl.cpp` | Add time display, history browsing |
| `Services/service/LcdService.hpp` | Add BaseTimer to input struct |
| `Services/controller/Controller.cpp` | Wire BaseTimer → LCD |
| `Services/driver/SdFalAdapter.hpp` | TSDB_SIZE → 2MB |
| `Services/model/F103Models.hpp` | Add ButtonEventModel (Step 7) |

## Files to Reference (existing, reuse)

| File | Reuse |
|------|-------|
| `Services/common/ChaCha20.hpp` | Encryption (already working) |
| `Services/driver/Esp8266.hpp` | `sendCmd()`, `getResponse()`, `responseContains()` |
| `Services/service/impl/TimerServiceImpl.hpp` | BaseTimer observable (1s) |
| `Middlewares/Third_Party/FlashDB/src/fdb_tsdb.c` | `fdb_tsl_iter_by_time()` API |

## Implementation Order

1. **SystemClock.hpp** — no dependencies, header-only
2. **NTP sync in WifiMqttService** — needs SystemClock, test with LCD status debug
3. **LCD time display** — needs SystemClock + BaseTimer wiring
4. **TSDB epoch timestamps** — needs SystemClock, update fdbGetTime()
5. **Midnight auto-export** — needs epoch timestamps, update taskLoop + exportDailyFile
6. **TSDB size increase** — one-line change, delete old tsdb.fdb on SD
7. **LCD history browsing** — deferred, independent feature

Steps 1-5 are the core deliverable. Step 6 is trivial. Step 7 can be a follow-up.

## Verification

1. **Build**: `make -j4 all` — no errors
2. **Flash + boot**: LCD shows "Arcana F103" title
3. **WiFi connect**: LCD shows MQTT status progression
4. **NTP sync**: LCD shows `HH:MM:SS` at top-right after WiFi connect
5. **TSDB writes**: Record count increases, `fdbGetTime()` returns epoch
6. **Midnight export**: Manually test by calling `exportDailyFile(today)`, verify `.enc` on SD
7. **Day-change**: Set NTP to near midnight, verify auto-export triggers
8. **Power-cycle**: Records persist, timestamp seeding works with epoch values
