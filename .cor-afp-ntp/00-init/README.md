# 00-init: 任務初始化

## 目的
定義任務範圍、識別目標檔案、載入必要參考資料

## 執行步驟

1. **明確任務目標**
   - 理解用戶需求
   - 確認驗收標準
   - 定義完成條件

2. **識別目標檔案**
   - 需要修改的現有檔案
   - 需要建立的新檔案
   - 相關的配置檔案

3. **載入技術參考**
   - 相關的程式碼檔案
   - 技術文件或規格
   - 範例程式碼

4. **確認環境準備**
   - 開發環境就緒
   - 相依套件安裝
   - 編譯工具可用

## 產出物檢查清單

- [ ] requirements.md - 需求說明
- [ ] target-files.json - 目標檔案清單
- [ ] references.md - 參考資料清單

## 完成後

執行驗證：
```bash
bash .cor-afp-ntp/00-init/exit.sh
```

然後轉換到下一節點：
```bash
bash .cor-protocol/scripts/node-transition.sh 00-init 01-analysis
```
