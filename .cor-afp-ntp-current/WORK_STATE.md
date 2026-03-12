# Lazy Virtual FAL + SDIO 高頻寫入 工作狀態

## 最後更新
2026-03-11 (Records 顯示問題調查中)

## 任務目標
1. 解決 STM32F103 SDIO DMA ~600 次寫入限制 → **已解決 (Lazy Virtual FAL)**
2. 擴大 TSDB 到 2MB → **已完成**
3. SDIO 軟重置 (OpenOCD flash 不需斷電) → **已解決**
4. 支持高頻寫入 (ADS1298 等) → **進行中**

## Git 狀態

### 已提交 (branch: debug-lcd-records)
```
342ff8a Remove per-write f_sync to reduce SDIO DMA writes, add periodic sync
3923d7e Implement Lazy Virtual FAL for 2MB TSDB and fix SDIO soft-reset
```

### 未提交的修改
- `sd_diskio.c`: `extern DMA_HandleTypeDef g_hdma_sdio` 從 ensure_hal_ready() 內部移到文件作用域 (微小重構)
- DMA periodic reinit 已完全移除 (aggressive 和 light 版本都破壞了 Records)

## 已驗證成功的里程碑
- [x] Lazy Virtual FAL 完整實作 (bitmap + virtual read + materialize on write)
- [x] 2MB TSDB (512 sectors) 啟動正常
- [x] 斷電再開正常 (bitmap 重建)
- [x] Records 可超過 379 條 (之前限制)
- [x] OpenOCD 燒錄後不需斷電 (SDIO 寄存器手動清除)
- [x] Deferred f_sync: Records 從 357 繼續增長 (每 30 秒 sync)

## 當前狀態: Records 顯示正常 ✅

### 問題解決
- **症狀**: DMA reinit 實驗後 Records 無法顯示
- **原因**: DMA reinit 破壞了系統狀態，需要完全斷電重啟
- **解決**: 重置到 commit 342ff8a + 完全斷電重啟

### 驗證結果
- ✅ 重置到 342ff8a (Deferred sync 版本)
- ✅ 編譯成功
- ✅ 燒錄成功
- ✅ **完全斷電重啟後 Records 顯示正常**

## 修改的檔案清單

### Commit 3923d7e (Lazy Virtual FAL)
```
Targets/stm32f103ze/
├── Middlewares/Third_Party/FatFs/src/ffconf.h     # FF_USE_EXPAND = 1
├── Services/driver/SdFalAdapter.hpp               # 2MB TSDB, bitmap, sync()
├── Services/driver/SdFalAdapter.cpp               # Full Lazy Virtual FAL
├── Services/driver/SdCard.cpp                     # SDIO soft-reset registers
├── Services/service/impl/LcdServiceImpl.cpp       # NTP unsynced placeholder
└── Services/service/impl/SdBenchmarkServiceImpl.cpp # Mount retry + deadlock fix
```

### Commit 342ff8a (Deferred sync)
```
Targets/stm32f103ze/
├── Services/driver/SdFalAdapter.cpp               # Remove f_sync from write()
└── Services/service/impl/SdStorageServiceImpl.cpp  # Periodic sync every 30s
```

### Uncommitted Changes
無 (已重置到 commit 342ff8a)

## 架構筆記

### Lazy Virtual FAL 核心機制
- `f_expand()` 分配檔案空間但不寫入資料 (零 DMA)
- RAM bitmap 追蹤已物化 vs 虛擬扇區
- Virtual read → memset 0xFF (無 SD I/O)
- Write → 先物化 (寫 0xFF) 再寫資料
- Erase → 清 bitmap bit (無 SD I/O)
- Periodic sync 每 30 秒 flush FatFS buffer

### SDIO I/O 策略
- Read: Polling at ~3.8MHz (CLKDIV=17) — 避免 DMA 方向切換問題
- Write: DMA at 24MHz (CLKDIV=1)
- `ensure_hal_ready()`: 每次操作前清除 DCTRL + flags

### ADC Simulator 除錯狀態
- **最新 Commit**: `211368b` - Debug ADC simulator - fix publish logic and data handling
- **問題**: ADC 寫入 2 次後死鎖 (Records: 232→234→卡死)
- **原因**: ObservableDispatcher 與 SdStorage task 競態條件
- **已嘗試**: 修復 publish 邏輯、實際數據複製、task 處理邏輯
- **結果**: 仍死鎖，已禁用 ADC simulator

### 未來工作
若要完整測試 batch write:
1. 方案 A: 使用現有 SensorData 模擬 ADC (複用穩定 Observable 鏈路)
2. 方案 B: 簡化 ADC 處理，單樣本直接寫入
3. 方案 C: 使用真實 ADS1298 硬體測試

## 如何恢復

```bash
cd /Users/jrjohn/Documents/projects/arcana-embedded-stm32
git checkout debug-lcd-records

# 查看未提交修改
git diff

# 建置與燒錄
cd Targets/stm32f103ze/Debug
export PATH="/Applications/STM32CubeIDE.app/.../tools/bin:$PATH"
make -j8 all
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/stm32f1x.cfg \
  -c "program arcana-embedded-f103.elf verify reset exit"
```

---
*COR+AFP+NTP 協議管理*
