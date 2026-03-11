# Bug 修復需求

## 問題
LCD Records 數字不會隨資料寫入而變動

## 根因
Stats 只在每秒窗口更新時發布，而不是每次寫入後立即更新

## 修復內容
修改 `SdStorageServiceImpl::appendRecord()`:
- 每次成功寫入後立即更新 `recordCount` 並發布 stats
- 保持 `writesPerSec` 每秒計算一次

## 目標檔案
- `Targets/stm32f103ze/Services/service/impl/SdStorageServiceImpl.cpp` (line 249-252)

## 驗收標準
- [ ] 每次寫入記錄後 LCD 立即更新 recordCount
- [ ] writesPerSec 仍正確顯示每秒寫入率
