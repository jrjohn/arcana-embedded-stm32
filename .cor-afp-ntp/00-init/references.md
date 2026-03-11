# 參考資料

## 技術文件

### 1. SystemClock.hpp
- 位置: `Services/common/SystemClock.hpp`
- 狀態: ✅ 已完成
- 功能: 提供 epoch 時間、日期轉換

### 2. PLAN-NTP-TSDB-Export.md
- 位置: `Targets/stm32f103ze/PLAN-NTP-TSDB-Export.md`
- 狀態: 規劃文件
- 內容: 詳細實作步驟

### 3. FlashDB TSDB
- 文件: `fdb_tsl_iter_by_time()` API
- 用途: 按時間範圍查詢記錄

## 現有實作參考

### ESP8266 通訊
```cpp
// Services/driver/Esp8266.hpp
sendCmd(), getResponse(), responseContains()
```

### ChaCha20 加密
```cpp
// Services/common/ChaCha20.hpp
// 已用於加密匯出檔案
```

### BaseTimer
```cpp
// Services/service/impl/TimerServiceImpl.hpp
// 1 秒計時器 observable
```
