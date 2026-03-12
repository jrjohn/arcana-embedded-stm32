# Lazy Virtual FAL + SDIO 高頻寫入 工作狀態

## 最後更新
2026-03-12

## 任務目標
1. 解決 STM32F103 SDIO DMA ~600 次寫入限制 → **已解決 (Lazy Virtual FAL)**
2. 擴大 TSDB 到 2MB → **已完成**
3. SDIO 軟重置 (OpenOCD flash 不需斷電) → **已解決**
4. 修復 erase bug (171K phantom records) → **已解決**
5. 解耦 sensor data 與 SD 寫入壓測 → **已解決**
6. 高頻壓測 50/s → **進行中 (publish flooding 修復待驗證)**

## Git 狀態

### 已提交 (branch: debug-lcd-records)
```
dbc7fe3 Add SD write stress test decoupled from sensor data
db5d2d5 Fix Lazy Virtual FAL erase: write 0xFF to disk instead of bitmap-only
5855560 Update COR+AFP+NTP documentation with sector boundary bug findings
812ffea Add error tracking and sector size experiments
9000484 Disable ADC simulation after finding long-term issue
```

### 未提交的修改
- `Controller.cpp`: `enableStressTest(50)` (從 10 改為 50)
- `SdStorageServiceImpl.cpp`: `updateStats()` 加入 publish decimation (每 10 次才 publish 一次)

## 已驗證成功的里程碑
- [x] Lazy Virtual FAL 完整實作 (bitmap + virtual read + materialize on write)
- [x] 2MB TSDB (512 sectors) 啟動正常
- [x] 斷電再開正常 (bitmap 重建)
- [x] Records 可超過 379 條 (之前限制)
- [x] OpenOCD 燒錄後不需斷電 (SDIO 寄存器手動清除)
- [x] Deferred f_sync: 每 30 秒 sync
- [x] **Erase bug 修復**: erase() 寫 0xFF 到磁碟 (不再只清 bitmap)
- [x] **Stress test 解耦**: 內部 dummy writes，sensor 維持 1 SPS for LCD/MQTT
- [x] **10/s stress test**: 穩定運作
- [ ] **50/s stress test**: 待驗證 (publish flooding fix 已部署)

## 當前狀態: 50/s 壓測 publish flooding 修復中

### 問題時間線

#### Bug 1: 171K phantom records (已解決)
- **症狀**: 重上電後 Records 從上次繼續累加，最終到 171,531 停止 (FDB_SAVED_FULL)
- **根因**: `erase()` 只清 RAM bitmap 不寫 0xFF 到磁碟。斷電後 bitmap 重建為 all-materialized → FlashDB 看到 phantom records
- **修復**: `erase()` 改為對已物化 sectors 寫 0xFF 到磁碟 (commit `db5d2d5`)
- **狀態**: ✅ 已修復已提交

#### Bug 2: Records 在 1026 停止 @ 50/s (修復中)
- **症狀**: stress test 從 10/s 拉到 50/s (實際 ~38/s)，Records 在 1026 停止
- **根因**: `updateStats()` 每次 `appendRecord()` 都 publish → 38+ publish/sec → ObservableDispatcher 隊列溢出 (8 slots)
- **修復**: 加入 publish decimation (每 10 次 updateStats 才 publish 一次，約 4/sec)
- **狀態**: ⏳ 已燒錄，待使用者回報結果

#### 實驗: 移除 materializeSector f_sync → 更差
- 移除 f_sync 後 Records 只到 68 就停止 (vs 有 f_sync 的 1026)
- f_sync 有 pacing 效果，已恢復

### SD 卡格式注意事項
- FatFS 設定 `FF_MIN_SS=FF_MAX_SS=512`
- SD 卡必須用 **預設配置大小** 格式化 (不要選 4KB 或其他)
- 格式化不對會導致 Records 完全不動

## 修改的檔案清單

### Commit db5d2d5 (Erase fix)
```
Targets/stm32f103ze/Services/driver/SdFalAdapter.cpp  # erase() 寫 0xFF + DELETE_FDB_ON_BOOT
```

### Commit dbc7fe3 (Stress test decoupling)
```
Targets/stm32f103ze/
├── Services/controller/Controller.cpp           # enableStressTest(10)
├── Services/service/SdStorageService.hpp         # enableStressTest() 虛函數
├── Services/service/impl/SdStorageServiceImpl.hpp # mStressTestHz + appendDummyRecord
├── Services/service/impl/SdStorageServiceImpl.cpp # taskLoop stress test + appendDummyRecord
```

### Uncommitted Changes
```
Targets/stm32f103ze/
├── Services/controller/Controller.cpp            # enableStressTest(50)
└── Services/service/impl/SdStorageServiceImpl.cpp # publish decimation (every 10th)
```

## 架構筆記

### Lazy Virtual FAL 核心機制
- `f_expand()` 分配檔案空間但不寫入資料 (零 DMA)
- RAM bitmap 追蹤已物化 vs 虛擬扇區
- Virtual read → memset 0xFF (無 SD I/O)
- Write → 先物化 (寫 0xFF + f_sync) 再寫資料
- **Erase → 已物化 sector 寫 0xFF 到磁碟** (保持 materialized，斷電安全)
- Periodic sync 每 30 秒 flush FatFS buffer

### Stress Test 架構
- `enableStressTest(hz)` 設定內部 dummy write 頻率
- `taskLoop()` 用 tick interval 控制寫入速率
- `appendDummyRecord()` 產生偽造 SensorDataModel 寫入 TSDB
- Sensor 資料 (1 SPS) 和 stress test 完全解耦
- LCD/MQTT 維持 1 SPS 實際溫度更新

### SDIO I/O 策略
- Read: Polling at ~3.8MHz (CLKDIV=17)
- Write: DMA at 24MHz (CLKDIV=1)
- `ensure_hal_ready()`: 每次操作前清除 DCTRL + flags

## 下一步

### 待驗證
1. **publish decimation 是否解決 1026 停止問題** ← 當前等待
2. **50/s 可持續多久** (目標: 超過 50K records 觸發 GC wrap)
3. **GC wrap 後斷電重啟**: 驗證 erase fix 在 GC 場景正常

### 後續目標
1. 100/s stress test
2. 移除 DELETE_FDB_ON_BOOT (測試完成後)
3. Commit 現有 uncommitted changes

## 如何恢復

```bash
cd /Users/jrjohn/Documents/projects/arcana-embedded-stm32
git checkout debug-lcd-records

# 查看未提交修改
git diff

# 建置與燒錄
cd Targets/stm32f103ze/Debug
export PATH="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools/bin:$PATH"
make -j8 all
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/stm32f1x.cfg \
  -c "program arcana-embedded-f103.elf verify reset exit"
```

---
*COR+AFP+NTP 協議管理*
