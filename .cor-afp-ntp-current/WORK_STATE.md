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

### 重大發現: Sector Boundary Bug (2026-03-11)
- **Commit**: `812ffea` - Add error tracking and sector size experiments
- **問題**: Records 在特定數字停止 (3033, 1130, 1225, 1360)
- **根因**: **3033 = 154 sectors 整數！** Lazy Virtual FAL 在 sector 邊界失敗
- **錯誤碼**: FlashDB error 3 (FDB_WRITE_ERR) at boundaries
- **系統狀態**: 其他任務正常，僅 FlashDB 寫入失敗

### 除錯實驗記錄
1. **添加錯誤追蹤**: LCD 顯示 ERR:X C:Y，INIT... 狀態
2. **512B sector 測試**: 導致初始化失敗，回復 4096B
3. **10 SPS 高頻模擬**: 快速暴露 sector 邊界問題
4. **確認**: Records 停止點都是 sector 邊界整數倍

### 技術細節
```
3033 records × 26 bytes = 78,858 bytes = 154 × 512B sectors (整數！)
1130 records = 57 sectors
1225 records = 62 sectors  
1360 records = 68 sectors
```

### 根因分析
Lazy Virtual FAL `materialize()` 在跨越 sector 邊界時：
- Bitmap 索引計算錯誤
- 或 f_expand 預分配空間邊界對齊問題
- 導致 FlashDB 無法寫入新 sector

### 解決方案 (建議)
1. **短期**: 限制 Records 在 3000 以下（手動重啟）
2. **中期**: 修復 `materialize()` sector 邊界處理
3. **長期**: 考慮移除 Lazy Virtual FAL，改用直接寫入

### 系統狀態
- ✅ 正常 1 SPS 溫度記錄：穩定運作
- ⚠️ 高頻寫入：會快速觸發 sector 邊界 bug
- ⚠️ 長期運行：約 3000+ 條後會停止

### ADC Simulator 除錯狀態
- **Commit**: `211368b` - Debug ADC simulator
- **問題**: ADC 寫入 2 次後死鎖
- **原因**: ObservableDispatcher 競態
- **結果**: 已禁用

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
