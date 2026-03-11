# 使用 STM32CubeIDE 編譯和燒錄

## 問題修復說明

已修復 **Records 和 Rate 不顯示數字** 的問題：

**原因**: `updateSdBenchmarkDisplay()` 和 `updateStorageDisplay()` 在同一位置繪製，導致互相覆蓋。

**修復**: 移除了 `updateSdBenchmarkDisplay()` 中繪製 Records/Rate 的程式碼，讓 `updateStorageDisplay()` 獨占顯示。

## 編譯步驟

### 1. 開啟專案

```bash
# 在終端機開啟 STM32CubeIDE (如果支援命令列)
open -a STM32CubeIDE

# 或在 STM32CubeIDE 中:
# File → Open Projects from File System
# 選擇: /Users/jrjohn/Documents/projects/arcana-embedded-stm32/Targets/stm32f103ze
```

### 2. 清理並編譯

在 STM32CubeIDE 中：
1. 在 Project Explorer 中右鍵點擊專案
2. 選擇 **Clean Project**
3. 等待清理完成
4. 選擇 **Build Project** (或按 Cmd+B)

### 3. 確認編譯成功

編譯成功後會產生：
- `Debug/arcana-embedded-f103.elf`
- `Debug/arcana-embedded-f103.bin`

## 燒錄步驟

### 方式 1: 使用 STM32CubeIDE

1. 連接 fireDAP 到電腦
2. 在 STM32CubeIDE 中選擇 **Run → Debug Configurations**
3. 建立新的 STM32 Cortex-M C/C++ Application 配置
4. Debugger 標籤:
   - Debug probe: ST-LINK (OpenOCD)
   - Interface: SWD
5. 點擊 **Debug** 開始燒錄和除錯

### 方式 2: 使用 OpenOCD (命令列)

編譯完成後，在終端機執行：

```bash
cd /Users/jrjohn/Documents/projects/arcana-embedded-stm32

# 燒錄新韌體
./flash-debug.sh flash
```

## 修改的檔案

**檔案**: `Targets/stm32f103ze/Services/service/impl/LcdServiceImpl.cpp`

**修改內容** (約 line 155-166): 刪除了以下程式碼：

```cpp
// 已刪除: SdBenchmark 的 Records/Rate 顯示
// Total records
uint32ToStr(buf, data->totalRecords);
mLcd.fillRect(VALUE_X + 60, SD_RECORDS_Y, 160, 8, Ili9341Lcd::BLACK);
mLcd.drawString(VALUE_X + 60, SD_RECORDS_Y, buf, ...);

// Records/sec
uint32ToStr(buf, data->recordsPerSec);
...
mLcd.fillRect(VALUE_X + 40, SD_RATE_Y, 160, 8, Ili9341Lcd::BLACK);
mLcd.drawString(VALUE_X + 40, SD_RATE_Y, buf, ...);
```

改為：

```cpp
// NOTE: Records/Rate display moved to updateStorageDisplay
// to avoid conflict with SdStorageService stats
```

## 預期結果

編譯並燒錄後，LCD 應該會顯示：
- ✅ Records: [數字]  (隨資料寫入增加)
- ✅ Rate: [數字]/s   (每秒寫入率)

## 驗證

1. 燒錄後觀察 LCD
2. 等待 Sensor 開始傳送資料
3. 確認 Records 數字會隨時間增加
4. Rate 應該顯示每秒寫入的記錄數

---

**注意**: 如果 STM32CubeIDE 編譯失敗，請確認：
1. 專案路徑正確
2. 所有必要的 include 路徑已設定
3. HAL 庫已正確配置
