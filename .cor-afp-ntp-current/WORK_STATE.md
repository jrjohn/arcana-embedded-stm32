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

### 高頻寫入測試結果

#### 方案 A: 使用 SensorData 模擬 ADC ✅ **成功**
- **Commit**: `f61789e` - Add ADC simulation mode to SensorService (Scheme A)
- **方法**: 複用現有 SensorData Observable 鏈路，避免新 Observer 競態
- **測試**: 10 SPS (10 寫入/秒) - **穩定運作**
- **結果**: Records 正常增長，無死鎖

#### AdcSimulatorService ❌ **失敗**
- **Commit**: `211368b` - Debug ADC simulator
- **問題**: ADC 寫入 2 次後死鎖 (Records: 232→234→卡死)
- **原因**: ObservableDispatcher 與 SdStorage task 競態條件
- **狀態**: 已禁用

### 如何使用 ADC 模擬模式
```cpp
// 在 Controller::initServices() 中啟用
mSensor->enableAdcSimulation(true, 10);  // 10 samples/sec
```

### 後續工作
- [ ] 測試更高採樣率 (50/100 SPS)
- [ ] 實現 Batch Write (緩存 N 樣本後批次寫入)
- [ ] 整合真實 ADS1298 硬體

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
