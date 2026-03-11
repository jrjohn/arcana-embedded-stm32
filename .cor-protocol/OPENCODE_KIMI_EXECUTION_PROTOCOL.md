# opencode CLI + kimi Code 執行協議

> **基於 COR + AFP + NTP 的任務執行標準**

適用環境：
- **CLI**: opencode v1.2.24
- **API**: kimi Code API
- **Platform**: macOS (darwin)
- **Context Limit**: ~128K tokens (kimi 模型)

---

## 📋 目錄

1. [執行架構總覽](#執行架構總覽)
2. [COR 節點鏈執行](#cor-節點鏈執行)
3. [AFP 狀態管理](#afp-狀態管理)
4. [NTP 轉換協議](#ntp-轉換協議)
5. [實際執行腳本](#實際執行腳本)
6. [任務啟動檢查清單](#任務啟動檢查清單)

---

## 執行架構總覽

### 環境結構

```
/Users/jrjohn/
├── .opencode/                    # opencode CLI
│   ├── bin/opencode             # CLI 執行檔 v1.2.24
│   └── ...
│
├── .claude/                      # Context & State
│   ├── skills/                  # Skill 定義
│   │   ├── skill-creator/
│   │   ├── app-uiux-designer/
│   │   └── ...
│   ├── projects/                # 專案狀態
│   ├── plans/                   # Plan mode 狀態
│   └── history.jsonl           # 對話歷史
│
└── Documents/projects/          # 工作目錄
    └── arcana-embedded-stm32/  # 當前專案
```

### 執行時 Context 組成

```
┌─────────────────────────────────────────────────────────────┐
│                    opencode CLI Session                      │
├─────────────────────────────────────────────────────────────┤
│  1. System Prompt (~5K tokens)                               │
│  2. Skill Metadata (~500 tokens)                            │
│  3. CLAUDE.md (~3K tokens)                                  │
│  4. 對話歷史 (可變動)                                        │
│  5. 當前請求 (~500 tokens)                                  │
│  6. Tool Results (視情況)                                    │
└─────────────────────────────────────────────────────────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │ kimi Code API        │
              │ Context Window: 128K │
              │ Compaction: >100K    │
              └─────────────────────┘
```

### Compaction 風險點

| 風險 | Token 閾值 | 影響 |
|------|-----------|------|
| 輕度 Compaction | ~80K | 最早對話開始壓縮 |
| 中度 Compaction | ~100K | 詳細指令可能遺失 |
| 重度 Compaction | ~120K | 僅保留最近對話 |

**關鍵**：在 ~80K tokens 前完成關鍵驗證和狀態保存

---

## COR 節點鏈執行

### 節點定義

每個任務拆解為標準節點：

```
00-init/           # 初始化
├── README.md      # 節點指引
├── checklist.md   # 執行檢查清單
└── exit.sh        # 退出驗證

01-analysis/       # 分析階段
02-design/         # 設計階段
03-impl/          # 實現階段
04-validation/    # 驗證階段
05-finalize/      # 完成階段
```

### 節點結構範本

```
task-workspace/
├── current-process.json    # AFP 狀態
├── phase-summary.md        # NTP 摘要
├── validation-chain.json   # 驗證歷史
│
├── 00-init/
│   ├── README.md          # 必讀
│   ├── checklist.md       # 執行清單
│   └── exit.sh           # 驗證腳本
│
└── 01-analysis/
    ├── README.md
    ├── context/          # 分析產物
    └── exit.sh
```

### 執行規則

1. **單節點載入**：一次只讀取當前節點的 README.md
2. **順序執行**：嚴格按照 00 → 01 → 02 順序
3. **阻斷檢查點**：每個節點必須通過 exit.sh 才能繼續

---

## AFP 狀態管理

### 狀態檔案位置

```bash
# 預設位置：專案根目錄的 .cor-afp-ntp/ 目錄
./.cor-afp-ntp/
├── current-process.json      # 主狀態檔
├── validation-chain.json     # 驗證歷史
├── phase-summary.md         # 階段摘要
└── checkpoints/             # 檢查點備份
    ├── 00-init.json
    ├── 01-analysis.json
    └── ...
```

### current-process.json Schema

```json
{
  "protocol_version": "1.0.0",
  "session_id": "uuid-v4",
  "current_node": "01-analysis",
  "task_name": "實作 UART 通訊模組",
  "project_path": "/Users/jrjohn/Documents/projects/arcana-embedded-stm32",
  
  "started_at": "2026-03-11T10:30:00+08:00",
  "updated_at": "2026-03-11T10:45:00+08:00",
  "token_estimate": 25000,
  
  "completed_nodes": ["00-init"],
  "node_outputs": {
    "00-init": {
      "requirements": ["實現 USART2 驅動", "支援 DMA", "中斷處理"],
      "target_files": ["Src/usart.c", "Inc/usart.h"],
      "references_loaded": ["STM32 HAL 文檔"]
    }
  },
  
  "validation_state": {
    "00-init": {
      "passed": true,
      "timestamp": "2026-03-11T10:35:00+08:00",
      "checks": ["requirements_clear", "files_identified", "references_loaded"]
    }
  },
  
  "recovery_hints": {
    "last_action": "完成需求分析，確認目標檔案",
    "pending_fixes": [],
    "files_modified": [],
    "critical_decisions": ["使用 HAL 庫而非 LL 庫"]
  },
  
  "context_window": {
    "estimated_tokens": 25000,
    "last_compaction": null,
    "warning_threshold": 80000
  }
}
```

### 保存時機

| 時機 | 動作 | 說明 |
|------|------|------|
| 節點開始 | 更新 `current_node` | 記錄進入時間 |
| 每 5 分鐘 | 保存 `current-process.json` | 定期備份 |
| 關鍵決策後 | 備份 checkpoint | 重要決策點 |
| 節點完成 | 更新所有狀態 | 通過驗證後 |
| 檢測到 Compaction | 立即保存 + 生成摘要 | 緊急保護 |

---

## NTP 轉換協議

### 轉換流程

```
完成當前節點工作
      │
      ▼
執行 exit.sh (節點驗證)
      │
      ├─ 驗證失敗
      │   ├── 顯示錯誤訊息
      │   ├── 記錄問題到 recovery_hints
      │   └── 修復後重新驗證
      │
      ▼ PASSED
生成 phase-summary.md
      │
      ▼
更新 current-process.json
  - current_node: 下一節點
  - completed_nodes: 加入當前
  - validation_state: 標記通過
      │
      ▼
顯示 Context Compact Point
      │
      ▼
讀取下一節點 README.md
      │
      ▼
繼續執行
```

### exit.sh 範本

```bash
#!/bin/bash
# 節點退出驗證腳本

set -e

PROJECT_PATH="${1:-.}"
WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"
CURRENT_NODE="01-analysis"

echo "═══════════════════════════════════════════════════════════════"
echo "  NTP Exit Validation: $CURRENT_NODE"
echo "═══════════════════════════════════════════════════════════════"

ERRORS=()

# Check 1: 需求文件是否存在
if [ ! -f "$WORKSPACE/01-analysis/requirements.md" ]; then
    ERRORS+=("❌ requirements.md 不存在")
fi

# Check 2: 目標檔案是否已識別
if ! grep -q "target_files" "$WORKSPACE/current-process.json" 2>/dev/null; then
    ERRORS+=("❌ 未識別 target_files")
fi

# Check 3: 關鍵決策是否記錄
if ! grep -q "critical_decisions" "$WORKSPACE/current-process.json" 2>/dev/null; then
    ERRORS+=("⚠️ 建議記錄 critical_decisions")
fi

# 輸出結果
if [ ${#ERRORS[@]} -eq 0 ]; then
    echo ""
    echo "✅ All validation checks passed!"
    echo ""
    echo "Ready to transition to next node."
    exit 0
else
    echo ""
    echo "❌ Validation failed with ${#ERRORS[@]} error(s):"
    for error in "${ERRORS[@]}"; do
        echo "   $error"
    done
    echo ""
    echo "Please fix the issues before proceeding."
    exit 1
fi
```

### Phase Summary 格式

```markdown
## Completed: 01-analysis (需求分析)

**Timestamp**: 2026-03-11T10:35:00+08:00
**Duration**: 15 minutes

### 完成項目
- ✅ 需求文件: requirements.md
- ✅ 目標檔案: Src/usart.c, Inc/usart.h
- ✅ 參考資料: STM32 HAL 參考手冊

### 關鍵決策
1. 使用 HAL 庫而非 LL 庫（可移植性優先）
2. 採用 DMA 模式減少 CPU 負載

### 上下文摘要
- 專案: arcana-embedded-stm32
- 平台: STM32F4xx
- 通訊介面: USART2

---

## Next: 02-design (架構設計)

**Expected Actions**:
1. 設計 USART 初始化流程
2. 規劃 DMA 緩衝區結構
3. 定義中斷處理程序

**Blocking Conditions**:
- 必須確認 GPIO 腳位配置
- 必須確認時鐘源設定

**Estimated Duration**: 20 minutes
```

### Context Compact Point

在節點轉換時輸出：

```
╔════════════════════════════════════════════════════════════╗
║           CONTEXT COMPACT POINT                             ║
║           (Preserve this information)                       ║
╚════════════════════════════════════════════════════════════╝

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
PHASE SUMMARY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[Phase Summary 內容]

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CURRENT STATE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Node: 01-analysis → 02-design
Project: arcana-embedded-stm32
Token Estimate: 25,000 / 128,000

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CRITICAL CONTEXT (DO NOT FORGET)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. 使用 HAL 庫實作 USART2
2. DMA 模式，緩衝區大小 256 bytes
3. 目標檔案: Src/usart.c, Inc/usart.h

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
NEXT ACTIONS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. Read: .cor-afp-ntp/02-design/README.md
2. Execute design checklist
3. Run exit.sh on completion

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

## 實際執行腳本

### 1. 任務初始化腳本

```bash
#!/bin/bash
# init-cor-afp-ntp.sh
# 初始化 COR+AFP+NTP 工作環境

TASK_NAME="$1"
PROJECT_PATH="${2:-.}"

if [ -z "$TASK_NAME" ]; then
    echo "Usage: $0 <task-name> [project-path]"
    exit 1
fi

WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"
mkdir -p "$WORKSPACE/checkpoints"

# 生成 session ID
SESSION_ID=$(uuidgen)
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%S+08:00")

# 創建 current-process.json
cat > "$WORKSPACE/current-process.json" << EOF
{
  "protocol_version": "1.0.0",
  "session_id": "$SESSION_ID",
  "current_node": "00-init",
  "task_name": "$TASK_NAME",
  "project_path": "$(cd "$PROJECT_PATH" && pwd)",
  "started_at": "$TIMESTAMP",
  "updated_at": "$TIMESTAMP",
  "token_estimate": 0,
  "completed_nodes": [],
  "node_outputs": {},
  "validation_state": {},
  "recovery_hints": {
    "last_action": "Task initialized",
    "pending_fixes": [],
    "files_modified": [],
    "critical_decisions": []
  },
  "context_window": {
    "estimated_tokens": 0,
    "last_compaction": null,
    "warning_threshold": 80000
  }
}
EOF

# 創建節點目錄
for node in 00-init 01-analysis 02-design 03-impl 04-validation 05-finalize; do
    mkdir -p "$WORKSPACE/$node"
done

# 創建 00-init 範本
cat > "$WORKSPACE/00-init/README.md" << 'EOF'
# 00-init: 任務初始化

## 目的
定義任務範圍、識別目標檔案、載入必要參考資料

## 檢查清單
- [ ] 明確任務目標
- [ ] 識別需要修改的檔案
- [ ] 載入相關技術文件
- [ ] 確認環境準備完成

## 產出物
- requirements.md
- target-files.json
- references-loaded.json

## 完成後
執行: bash .cor-afp-ntp/00-init/exit.sh
EOF

cat > "$WORKSPACE/00-init/exit.sh" << 'EOF'
#!/bin/bash
WORKSPACE="$(dirname $0)/.."
ERRORS=()

[ ! -f "$WORKSPACE/00-init/requirements.md" ] && ERRORS+=("requirements.md 不存在")
[ ! -f "$WORKSPACE/00-init/target-files.json" ] && ERRORS+=("target-files.json 不存在")

if [ ${#ERRORS[@]} -eq 0 ]; then
    echo "✅ Validation passed"
    exit 0
else
    echo "❌ Validation failed:"
    printf '%s\n' "${ERRORS[@]}"
    exit 1
fi
EOF

chmod +x "$WORKSPACE/00-init/exit.sh"

echo "✅ COR-AFP-NTP workspace initialized"
echo "   Workspace: $WORKSPACE"
echo "   Session: $SESSION_ID"
echo ""
echo "Next: Read $WORKSPACE/00-init/README.md"
```

### 2. 節點轉換腳本

```bash
#!/bin/bash
# node-transition.sh
# 執行 NTP 節點轉換

FROM_NODE="$1"
TO_NODE="$2"
PROJECT_PATH="${3:-.}"

if [ -z "$FROM_NODE" ] || [ -z "$TO_NODE" ]; then
    echo "Usage: $0 <from-node> <to-node> [project-path]"
    echo "Example: $0 00-init 01-analysis"
    exit 1
fi

WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"
CURRENT_FILE="$WORKSPACE/current-process.json"

if [ ! -f "$CURRENT_FILE" ]; then
    echo "❌ current-process.json not found. Run init first."
    exit 1
fi

# 執行 exit validation
EXIT_SCRIPT="$WORKSPACE/$FROM_NODE/exit.sh"
if [ -f "$EXIT_SCRIPT" ]; then
    echo "Running exit validation for $FROM_NODE..."
    if ! bash "$EXIT_SCRIPT" "$PROJECT_PATH"; then
        echo "❌ Exit validation failed. Cannot proceed."
        exit 1
    fi
else
    echo "⚠️  No exit.sh found for $FROM_NODE, skipping validation"
fi

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%S+08:00")

# 生成 phase summary
SUMMARY_FILE="$WORKSPACE/phase-summary.md"
cat > "$SUMMARY_FILE" << EOF
## Completed: $FROM_NODE

**Timestamp**: $TIMESTAMP
**Task**: $(jq -r '.task_name' "$CURRENT_FILE")

### 節點輸出
$(jq -r ".node_outputs[\"$FROM_NODE\"] // {}" "$CURRENT_FILE" | jq -r 'to_entries | .[] | "- \(.key): \(.value)"' 2>/dev/null || echo "- No outputs recorded")

### 驗證狀態
- Status: PASSED
- Time: $TIMESTAMP

---

## Next: $TO_NODE

$(if [ -f "$WORKSPACE/$TO_NODE/README.md" ]; then
    echo "### README Preview"
    head -20 "$WORKSPACE/$TO_NODE/README.md"
fi)

**Action**: Read $WORKSPACE/$TO_NODE/README.md and continue execution.
EOF

# 更新 current-process.json
TMP_FILE=$(mktemp)
jq --arg from "$FROM_NODE" \
   --arg to "$TO_NODE" \
   --arg time "$TIMESTAMP" \
   '
   .completed_nodes += [$from] |
   .current_node = $to |
   .updated_at = $time |
   .validation_state[$from] = {
     "passed": true,
     "timestamp": $time,
     "checks": ["exit_validation"]
   } |
   .recovery_hints.last_action = "Completed \($from), transitioning to \($to)"
   ' "$CURRENT_FILE" > "$TMP_FILE"

mv "$TMP_FILE" "$CURRENT_FILE"

# 顯示 Context Compact Point
cat << COMPACT

╔════════════════════════════════════════════════════════════╗
║           CONTEXT COMPACT POINT                             ║
║           (Preserve this information)                       ║
╚════════════════════════════════════════════════════════════╝

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
PHASE TRANSITION: $FROM_NODE → $TO_NODE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Completed: $FROM_NODE
Next: $TO_NODE
Timestamp: $TIMESTAMP

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
INSTRUCTIONS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. Read: .cor-afp-ntp/$TO_NODE/README.md
2. Follow the checklist in the README
3. Execute tasks one by one
4. Run: bash .cor-afp-ntp/$TO_NODE/exit.sh
5. Transition to next node when done

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

COMPACT

echo "✅ Transition complete: $FROM_NODE → $TO_NODE"
```

### 3. 快速健康檢查腳本

```bash
#!/bin/bash
# quick-health-check.sh
# 快速檢查 AFP 狀態

PROJECT_PATH="${1:-.}"
WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"

echo "═══════════════════════════════════════════════════════════════"
echo "  AFP Quick Health Check"
echo "═══════════════════════════════════════════════════════════════"

ERRORS=()
WARNINGS=()

# Check 1: Workspace exists
if [ ! -d "$WORKSPACE" ]; then
    ERRORS+=("❌ Workspace directory not found: $WORKSPACE")
else
    echo "✅ Workspace exists"
fi

# Check 2: current-process.json exists
if [ ! -f "$WORKSPACE/current-process.json" ]; then
    ERRORS+=("❌ current-process.json not found")
else
    echo "✅ current-process.json exists"
    
    # Validate JSON
    if ! jq empty "$WORKSPACE/current-process.json" 2>/dev/null; then
        ERRORS+=("❌ current-process.json is invalid JSON")
    else
        echo "✅ current-process.json is valid JSON"
        
        # Check required fields
        for field in session_id current_node task_name; do
            if ! jq -e ".$field" "$WORKSPACE/current-process.json" >/dev/null 2>&1; then
                ERRORS+=("❌ Missing field: $field")
            fi
        done
        
        # Show current status
        CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json")
        COMPLETED=$(jq -r '.completed_nodes | length' "$WORKSPACE/current-process.json")
        echo "   Current node: $CURRENT_NODE"
        echo "   Completed nodes: $COMPLETED"
    fi
fi

# Check 3: Current node directory exists
if [ -f "$WORKSPACE/current-process.json" ]; then
    CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json")
    if [ ! -d "$WORKSPACE/$CURRENT_NODE" ]; then
        WARNINGS+=("⚠️  Current node directory not found: $CURRENT_NODE")
    else
        echo "✅ Current node directory exists: $CURRENT_NODE"
    fi
fi

# Check 4: phase-summary.md exists
if [ ! -f "$WORKSPACE/phase-summary.md" ]; then
    WARNINGS+=("⚠️  phase-summary.md not found (needed for recovery)")
else
    echo "✅ phase-summary.md exists"
fi

# Output results
echo ""
if [ ${#ERRORS[@]} -eq 0 ] && [ ${#WARNINGS[@]} -eq 0 ]; then
    echo "✅ All checks passed! System is healthy."
    exit 0
else
    if [ ${#ERRORS[@]} -gt 0 ]; then
        echo "❌ ERRORS (${#ERRORS[@]}):"
        printf '%s\n' "${ERRORS[@]}"
    fi
    if [ ${#WARNINGS[@]} -gt 0 ]; then
        echo ""
        echo "⚠️  WARNINGS (${#WARNINGS[@]}):"
        printf '%s\n' "${WARNINGS[@]}"
    fi
    exit 1
fi
```

### 4. 狀態恢復腳本

```bash
#!/bin/bash
# recover-state.sh
# Compaction 後狀態恢復

PROJECT_PATH="${1:-.}"
WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"

echo "═══════════════════════════════════════════════════════════════"
echo "  AFP State Recovery"
echo "═══════════════════════════════════════════════════════════════"

# Step 1: Check workspace
if [ ! -d "$WORKSPACE" ]; then
    echo "❌ Workspace not found. Cannot recover."
    exit 1
fi

# Step 2: Read phase summary
if [ -f "$WORKSPACE/phase-summary.md" ]; then
    echo ""
    echo "📋 Phase Summary (Last checkpoint):"
    echo "─────────────────────────────────────────────────────────────"
    cat "$WORKSPACE/phase-summary.md"
    echo "─────────────────────────────────────────────────────────────"
fi

# Step 3: Read current state
if [ -f "$WORKSPACE/current-process.json" ]; then
    echo ""
    echo "📊 Current State:"
    echo "─────────────────────────────────────────────────────────────"
    jq -r '
    "Session: \(.session_id)",
    "Task: \(.task_name)",
    "Current Node: \(.current_node)",
    "Completed: \(.completed_nodes | join(", "))",
    "Last Action: \(.recovery_hints.last_action)"
    ' "$WORKSPACE/current-process.json"
    echo "─────────────────────────────────────────────────────────────"
fi

# Step 4: Validate state
echo ""
echo "🔍 Running health check..."
bash "$WORKSPACE/../quick-health-check.sh" "$PROJECT_PATH" 2>/dev/null || true

# Step 5: Output recovery instructions
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "RECOVERY INSTRUCTIONS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [ -f "$WORKSPACE/current-process.json" ]; then
    CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json")
    echo "1. Continue from current node: $CURRENT_NODE"
    echo "   Read: .cor-afp-ntp/$CURRENT_NODE/README.md"
    
    if [ -f "$WORKSPACE/$CURRENT_NODE/README.md" ]; then
        echo ""
        echo "2. Node README Preview:"
        echo "─────────────────────────────────────────────────────────────"
        head -15 "$WORKSPACE/$CURRENT_NODE/README.md"
        echo "─────────────────────────────────────────────────────────────"
    fi
else
    echo "⚠️  No state file found. You may need to restart the task."
fi

echo ""
echo "3. Critical Context to Remember:"
echo "   (From phase-summary.md)"
grep -A 20 "CRITICAL CONTEXT\|關鍵決策" "$WORKSPACE/phase-summary.md" 2>/dev/null || echo "   (No critical context recorded)"

echo ""
echo "✅ Recovery information loaded. Continue execution."
```

---

## 任務啟動檢查清單

### 開始新任務前

```markdown
## Pre-Flight Checklist

### 環境檢查
- [ ] opencode CLI 正常運作 (opencode --version)
- [ ] 在正確的專案目錄中
- [ ] 有足夠的磁碟空間
- [ ] 網路連線正常 (API 調用)

### 協議初始化
- [ ] 執行 init-cor-afp-ntp.sh <task-name>
- [ ] 確認 .cor-afp-ntp/ 目錄已建立
- [ ] 檢查 current-process.json 已生成
- [ ] 閱讀 00-init/README.md

### Context 管理
- [ ] 預估任務 token 消耗
- [ ] 設定 compaction 預警閾值 (80K)
- [ ] 規劃 checkpoint 時機
- [ ] 確認 recovery hints 會記錄關鍵決策

### 執行準備
- [ ] 準備相關技術文件
- [ ] 識別目標檔案
- [ ] 設定預計完成時間
- [ ] 準備好開始執行
```

### 執行中檢查點

```markdown
## In-Progress Checklist

### 每 10 分鐘或每個節點後
- [ ] 更新 current-process.json
- [ ] 檢查 token 使用量
- [ ] 記錄關鍵決策到 recovery_hints
- [ ] 執行 exit.sh 驗證

### 節點轉換時
- [ ] exit.sh 通過
- [ ] 生成 phase-summary.md
- [ ] 執行 node-transition.sh
- [ ] 顯示 Context Compact Point
- [ ] 讀取下一節點 README.md

### 檢測到異常時
- [ ] 立即保存 current-process.json
- [ ] 生成 phase-summary.md (緊急)
- [ ] 執行 quick-health-check.sh
- [ ] 記錄問題到 recovery_hints
```

### 任務結束時

```markdown
## Post-Completion Checklist

### 驗證
- [ ] 所有節點標記為完成
- [ ] 最終 validation 通過
- [ ] 產出物確認無誤

### 文檔
- [ ] 最終 phase-summary.md 已生成
- [ ] 關鍵決策已記錄
- [ ] 遇到的問題和解決方案已記錄

### 清理 (可選)
- [ ] 備份 .cor-afp-ntp/ 到 .cor-afp-ntp.bak/
- [ ] 清理不必要的 checkpoint 檔案
- [ ] 記錄任務完成時間
```

---

## 實際操作範例

### 範例 1：新增 UART 驅動功能

```bash
# 1. 初始化
bash init-cor-afp-ntp.sh "實作 USART2 DMA 驅動"

# 2. AI 執行 00-init
# - 讀取 .cor-afp-ntp/00-init/README.md
# - 識別目標檔案: Src/usart.c, Inc/usart.h
# - 載入 STM32 HAL 參考

# 3. 節點完成，執行驗證
bash .cor-afp-ntp/00-init/exit.sh

# 4. 轉換到下一節點
bash node-transition.sh 00-init 01-analysis

# 5. AI 執行 01-analysis
# - 讀取 .cor-afp-ntp/01-analysis/README.md
# - 分析現有程式碼結構
# - 設計驅動架構

# 6. 依此類推...
```

### 範例 2：Compaction 發生後恢復

```bash
# AI 檢測到 Compaction (token > 80K)
# → 立即執行：

# 1. 保存當前狀態
cat > .cor-afp-ntp/phase-summary.md << 'EOF'
## Emergency Checkpoint

Compaction detected at ~80K tokens.
Current node: 03-impl
Last action: 實作 USART_Init() 函數
Critical context:
- 使用 HAL_UART_Init()
- DMA 模式開啟
- 目標檔案: Src/usart.c (已修改)
EOF

# 2. 快速檢查
bash .cor-afp-ntp/quick-health-check.sh

# 3. 恢復執行
bash .cor-afp-ntp/recover-state.sh

# 4. AI 繼續執行，從 03-impl 繼續
```

---

## 常見問題 (FAQ)

### Q1: Token 使用量如何估算？

A: 簡易公式：
- 每個中文字符 ≈ 1.5 tokens
- 每行程式碼 ≈ 10-20 tokens
- 對話歷史 ≈ 累計總和

建議在 60K 時開始規劃 checkpoint

### Q2: 可以跳過某些節點嗎？

A: **不建議**。COR 架構設計為順序執行。若確實需要：
1. 執行 node-transition.sh 強制跳過
2. 在 phase-summary.md 記錄原因
3. 更新 current-process.json

### Q3: 多個檔案如何管理？

A: 在 00-init 節點識別所有目標檔案，記錄在 `target-files.json`：
```json
{
  "primary": ["Src/main.c", "Inc/main.h"],
  "secondary": ["Src/gpio.c"],
  "generated": ["Src/usart.c"]
}
```

### Q4: 如何處理長時間任務？

A: 策略：
1. 將大任務拆分為多個 sub-task
2. 每個 sub-task 獨立執行 COR-AFP-NTP
3. 使用 dependencies.json 記錄依賴關係

---

## 附錄

### 工具依賴

```bash
# 必要工具
jq          # JSON 處理
uuidgen     # UUID 生成 (macOS 內建)
date        # 時間戳記

# 安裝 jq (若未安裝)
brew install jq  # macOS
```

### 環境變數

```bash
# 可選設定
export COR_AFP_NTP_WORKSPACE=".cor-afp-ntp"
export COR_AFP_NTP_TOKEN_THRESHOLD=80000
export COR_AFP_NTP_AUTO_CHECKPOINT=true
```

### 整合到 CI/CD

```yaml
# .github/workflows/cor-afp-ntp.yml
name: COR-AFP-NTP Validation

on: [push]

jobs:
  validate:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Check AFP State
        run: |
          bash .cor-afp-ntp/quick-health-check.sh
      
      - name: Validate All Nodes
        run: |
          for node in .cor-afp-ntp/*/exit.sh; do
            bash "$node" || exit 1
          done
```

---

**文件版本**: 1.0.0
**適用環境**: opencode CLI 1.2.24 + kimi Code API
**最後更新**: 2026-03-11
