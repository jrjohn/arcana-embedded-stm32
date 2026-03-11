# 使用 STM32CubeIDE + fireDAP Debug 指南

## ✅ 目前狀態

OpenOCD 已成功啟動並連接到您的 STM32F103:
- fireDAP: CMSIS-DAP v2.0.0 (已連接)
- 目標: STM32F103 (Cortex-M3 r1p1)
- GDB Server: localhost:3333
- Telnet: localhost:4444

## 🔧 Debug 方案

由於缺少 arm-none-eabi-gdb，建議使用 **STM32CubeIDE** 進行除錯：

### 方案 1: STM32CubeIDE + OpenOCD (推薦)

1. **打開 STM32CubeIDE**
   ```
   open -a STM32CubeIDE
   ```

2. **配置 Debug 設定**
   - Run → Debug Configurations
   - 雙擊 "STM32 Cortex-M C/C++ Application"
   - Main 標籤:
     - Project: 選擇您的專案
     - C/C++ Application: `Debug/arcana-embedded-f103.elf`
   
   - Debugger 標籤:
     - Debug probe: ST-LINK (OpenOCD)
     - Interface: SWD
     - 端口: 3333 (因為 OpenOCD 已在運行)

3. **開始除錯**
   - 點擊 "Debug" 按鈕
   - 程式會暫停在 main() 或 Reset_Handler

4. **檢查 FSDB 狀態**
   在 Expressions 視窗添加以下變數：
   ```
   arcana::sdstorage::SdStorageServiceImpl::getInstance().mDbReady
   arcana::sdstorage::SdStorageServiceImpl::getInstance().mNonceCounter
   arcana::sdstorage::SdStorageServiceImpl::getInstance().mStats.recordCount
   arcana::sdstorage::SdStorageServiceImpl::getInstance().mStats.writesPerSec
   g_exfat_ready
   ```

### 方案 2: 在程式碼中觀察

我們已在程式碼中添加了 LCD Debug 輸出：

**請確認您已編譯並燒錄最新版本 (包含 Debug 輸出)**

觀察 LCD 下方 (y=180) 的紅色文字：

| 顯示 | 含義 | 狀態 |
|------|------|------|
| `SD: FAL OK` | SD 卡適配器初始化成功 | ✅ 正常 |
| `SD: TSDB OK` | TSDB 初始化成功 | ✅ 正常 |
| `SD: KVDB OK` | KVDB 初始化成功 | ✅ 正常 |
| `SD: rec=N` | 初始記錄數為 N | ℹ️ 資訊 |
| `W:10` | 已寫入 10 條記錄 | ✅ 正常 |
| `WR err=X` | 寫入錯誤，錯誤碼 X | ❌ 錯誤 |

### 方案 3: 命令列工具 (需要安裝)

如果需要命令列 GDB，可以安裝：

```bash
# 使用 Homebrew 安裝 ARM GCC (包含 GDB)
brew install gcc-arm-embedded

# 然後可以使用:
arm-none-eabi-gdb arcana-embedded-f103.elf
(gdb) target remote localhost:3333
```

## 🎯 快速診斷

**請在 STM32CubeIDE 中觀察以下變數：**

### 情況 A: mDbReady = 0
- **原因**: FSDB 初始化失敗
- **檢查**: LCD 是否顯示 `SD: TSDB err=X`
- **解決**: 檢查 SD 卡、exFAT 檔案系統

### 情況 B: mDbReady = 1, mNonceCounter = 0
- **原因**: FSDB 正常，但沒有資料寫入
- **檢查**: 
  - Sensor 是否正常工作 (溫度是否有顯示)
  - Controller.cpp 中 SdStorage 是否訂閱了 SensorData
  - exFAT 是否已掛載 (g_exfat_ready)

### 情況 C: mDbReady = 1, mNonceCounter > 0
- **原因**: 資料正在寫入！
- **檢查**: LCD Records 數字為何不更新
- **解決**: 確認 LcdServiceImpl.cpp 修復已應用

## 📋 建議步驟

1. **使用 STM32CubeIDE 開始 Debug**
2. **觀察變數 mDbReady 的值**
3. **告訴我觀察結果，我幫您分析**

## 💡 替代方案

如果暫時無法 Debug，可以：

1. **重新編譯並燒錄 Debug 版本**
   - 確認已應用所有修復
   - 包含 LCD Debug 輸出

2. **直接觀察 LCD**
   - 告訴我下方紅色文字顯示什麼
   - 我幫您判斷問題所在

## 🔥 關鍵檢查點

**現在請幫我確認：**

1. **開發板上的 LCD 目前顯示什麼？**
   - 是否有溫度數值？
   - 下方是否有紅色 Debug 文字？
   - Records: 後面是數字還是空白？

2. **或者使用 STM32CubeIDE Debug，變數 mDbReady 是多少？**

告訴我結果，我可以立即幫您定位問題！
