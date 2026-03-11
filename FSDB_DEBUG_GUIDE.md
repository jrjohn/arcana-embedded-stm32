# FSDB Debug 版本 - 編譯和測試指南

## 修改內容

已添加完整的 FSDB 寫入診斷資訊，幫助確認問題所在。

### 新增的 Debug 輸出

在 LCD 下方區域 (y=180) 會顯示紅色文字：

| 訊息 | 含義 |
|------|------|
| `SD: FAL init fail` | FAL 適配器初始化失敗 |
| `SD: FAL OK` | FAL 初始化成功 |
| `SD: TSDB err=X` | TSDB 初始化失敗，錯誤碼 X |
| `SD: TSDB OK` | TSDB 初始化成功 |
| `SD: KVDB err=X` | KVDB 初始化失敗，錯誤碼 X |
| `SD: KVDB OK` | KVDB 初始化成功 |
| `SD: rec=N` | 初始記錄數為 N |
| `W:N` | 成功寫入第 N 條記錄 (每10條顯示) |
| `WR err=X` | 寫入失敗，錯誤碼 X |

## FlashDB 錯誤碼

- `-4` (FDB_NO_ERR): 成功
- `-1` (FDB_ERR_NULL): 空指針
- `-2` (FDB_ERR_NOMEM): 內存不足
- `-3` (FDB_ERR_FLASH): Flash 操作失敗
- `-5` (FDB_ERR_FULL): 存儲已滿
- `-6` (FDB_ERR_INIT): 初始化錯誤
- `-7` (FDB_ERR_ASSERT): 斷言失敗

## 編譯步驟 (STM32CubeIDE)

1. **打開專案**
   ```
   STM32CubeIDE → File → Open Projects from File System
   選擇: Targets/stm32f103ze
   ```

2. **清理並編譯**
   - 右鍵專案 → Clean Project
   - 右鍵專案 → Build Project

3. **確認檔案**
   編譯成功後檢查:
   - `Debug/arcana-embedded-f103.elf`
   - 修改時間應該是最新的

## 燒錄步驟

### 方式 1: STM32CubeIDE
1. 連接 fireDAP
2. Run → Debug Configurations
3. 選擇 ST-LINK (OpenOCD), SWD 介面
4. 點擊 Debug

### 方式 2: 命令列 (OpenOCD)
```bash
cd /Users/jrjohn/Documents/projects/arcana-embedded-stm32
./flash-debug.sh flash
```

## 測試流程

### 第一步: 觀察初始化

燒錄後觀察 LCD 下方紅色文字：

**正常情況:**
```
SD: FAL OK
SD: TSDB OK
SD: KVDB OK
SD: rec=0
```

**異常情況 (範例):**
```
SD: FAL init fail    → SD 卡或 FAL 問題
SD: TSDB err=-3      → Flash 操作失敗
SD: KVDB err=-5      → 存儲空間已滿
```

### 第二步: 觀察寫入

如果初始化正常，等待 Sensor 資料：

**正常情況:**
```
W:10     (寫入10條)
W:20     (寫入20條)
W:30     (寫入30條)
...
```

**異常情況:**
```
WR err=-3    (Flash 寫入失敗)
WR err=-5    (存儲已滿)
```

### 第三步: 確認 Records 顯示

如果看到 `W:N` 訊息但 LCD Records 仍不顯示數字：
- 問題在顯示層 (LcdServiceImpl)
- 檢查 Controller.cpp 的連線

如果沒有看到 `W:N` 訊息：
- 問題在資料流 (Sensor → SdStorage)
- 檢查 Sensor 是否正常工作
- 檢查 exFAT 是否掛載成功

## 預期結果

### 情況 A: 初始化失敗
- **症狀**: 顯示 `SD: FAL init fail` 或錯誤碼
- **原因**: SD 卡未插入、檔案系統損壞、或硬件問題
- **解決**: 檢查 SD 卡、重新格式化為 exFAT

### 情況 B: 寫入失敗
- **症狀**: 顯示 `WR err=X`
- **原因**: Flash 錯誤、存儲已滿、或時間戳問題
- **解決**: 根據錯誤碼排查

### 情況 C: 顯示問題
- **症狀**: 看到 `W:N` 但 Records 數字不變
- **原因**: LCD 顯示被覆蓋或連線問題
- **解決**: 確認之前的修復已應用

## 故障排除

### 沒有 Debug 訊息

1. 確認編譯的是最新版本
2. 檢查 LCD 初始化是否正常
3. 確認 y=180 位置未被其他內容覆蓋

### 一直顯示 `SD: FAL init fail`

1. 確認 SD 卡已插入
2. 確認 SD 卡格式為 exFAT
3. 檢查 SdBenchmarkService 是否成功掛載 exFAT

### 顯示 `SD: TSDB err=-3`

錯誤碼 -3 表示 Flash 操作失敗，可能原因：
1. FAL 未正確初始化
2. SD 卡損壞
3. 檔案系統不支援

### 初始化成功但無 `W:N`

1. 確認 Sensor 資料是否正常 (檢查溫度顯示)
2. 確認 Controller.cpp 中 SdStorage 訂閱了 SensorData
3. 檢查是否有 `exFAT ready` 訊息

## 修改的檔案

- `SdStorageServiceImpl.cpp`: 添加 debug 輸出和錯誤處理
- `LcdServiceImpl.cpp`: 修復顯示衝突 (之前修改)

## 測試完成後

Debug 訊息會稍微影響性能，測試完成後可以：
1. 刪除或註釋掉 lcdDebug 呼叫
2. 重新編譯燒錄
3. 保留 LCD Records 和 Rate 的正常顯示
