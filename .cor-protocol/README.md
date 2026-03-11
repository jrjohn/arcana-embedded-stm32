# COR + AFP + NTP 執行協議

> **適用於 opencode CLI + kimi Code API 環境的標準化任務執行架構**

---

## 快速開始

### 1. 初始化新任務

```bash
# 初始化 COR-AFP-NTP 工作空間
bash .cor-protocol/scripts/init-cor-afp-ntp.sh "你的任務名稱"

# 例如
bash .cor-protocol/scripts/init-cor-afp-ntp.sh "實作 USART2 DMA 驅動"
```

### 2. AI 執行流程

AI 會依照以下流程執行：

```
00-init → 01-analysis → 02-design → 03-impl → 04-validation → 05-finalize
```

每個節點：
1. 讀取該節點的 `README.md`
2. 執行檢查清單
3. 完成後執行 `exit.sh` 驗證
4. 執行 `node-transition.sh` 轉換到下一節點

### 3. 常用命令

```bash
# 檢查健康狀態
bash .cor-protocol/scripts/quick-health-check.sh

# 節點轉換
bash .cor-protocol/scripts/node-transition.sh 00-init 01-analysis

# Compaction 後恢復
bash .cor-protocol/scripts/recover-state.sh
```

---

## 目錄結構

```
.cor-protocol/
├── OPENCODE_KIMI_EXECUTION_PROTOCOL.md  # 完整協議文件
├── README.md                           # 本文件
└── scripts/                            # 執行腳本
    ├── init-cor-afp-ntp.sh            # 初始化
    ├── node-transition.sh             # 節點轉換
    ├── quick-health-check.sh          # 健康檢查
    └── recover-state.sh               # 狀態恢復

.cor-afp-ntp/                           # 自動生成 (每個任務)
├── current-process.json               # 當前狀態
├── validation-chain.json              # 驗證歷史
├── phase-summary.md                   # 階段摘要
└── 00-init/                           # 節點目錄
    ├── README.md
    ├── checklist.md
    └── exit.sh
```

---

## 三大協議說明

### COR (Chain of Repository)

**目的**：節點鏈式執行，減少 Token 消耗

- 一次只載入當前節點的資訊
- 嚴格按照順序執行
- 每個節點有明確的進入/退出條件

### AFP (Anti-Forgetting Protocol)

**目的**：防止 Compaction 導致的資訊遺失

- `current-process.json`：保存當前狀態
- `phase-summary.md`：人類可讀的階段摘要
- `recover-state.sh`：Compaction 後恢復

### NTP (Node Transition Protocol)

**目的**：確保節點間安全轉換

- `exit.sh`：節點完成驗證
- `node-transition.sh`：執行轉換
- Context Compact Point：壓縮點提示

---

## 環境資訊

- **CLI**: opencode v1.2.24
- **API**: kimi Code API
- **Context Limit**: ~128K tokens
- **Compaction**: ~80-100K tokens 開始壓縮

---

## 詳細說明

參見完整協議文件：

```bash
cat .cor-protocol/OPENCODE_KIMI_EXECUTION_PROTOCOL.md
```

---

**版本**: 1.0.0  
**更新日期**: 2026-03-11
