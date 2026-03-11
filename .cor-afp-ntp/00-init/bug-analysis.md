# Bug 分析報告: LCD Records 數字不變動

## 問題描述
LCD 上的 Records 數字不會隨著資料寫入而更新。

## 根本原因分析

### 問題位置
`SdStorageServiceImpl::appendRecord()` (line 211-254)

### 問題說明
Stats 更新邏輯有以下問題：

1. **更新頻率過低**: Stats 只在每秒窗口更新時發布（line 243-246）
   ```cpp
   if (elapsedMs >= 1000) {
       mLastRate = mWritesInWindow;
       mWritesInWindow = 0;
       mWindowStartTick = now;
   }
   ```

2. **缺少即時更新**: `recordCount` 應該每次寫入後立即更新，而不是等待每秒窗口

3. **發布時機不對**: `mStatsObs.publish()` 只在窗口更新時執行，如果寫入頻率低於每秒一次，stats 可能長時間不更新

## 修復方案

### 方案 1: 每次寫入後立即更新 recordCount (推薦)
在每次成功寫入後立即更新 `recordCount` 並發布 stats，但保持 `writesPerSec` 每秒計算一次。

### 方案 2: 每秒強制發布 Stats
無論是否有新寫入，每秒都發布一次 stats（可在 taskLoop 中實現）。

## 建議採用方案 1
原因：
- 簡單且有效率
- recordCount 會即時反映
- writesPerSec 仍保持準確的每秒速率

## 目標檔案
- `Targets/stm32f103ze/Services/service/impl/SdStorageServiceImpl.cpp`
