#!/bin/bash
#
# node-transition.sh
# NTP (Node Transition Protocol) - 執行節點轉換
# 適用於 opencode CLI + kimi Code API 環境
#

set -e

# 顏色定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 參數
FROM_NODE="$1"
TO_NODE="$2"
PROJECT_PATH="${3:-.}"

# 使用說明
usage() {
    echo "Usage: $0 <from-node> <to-node> [project-path]"
    echo ""
    echo "Example:"
    echo "  $0 00-init 01-analysis"
    echo "  $0 01-analysis 02-design /path/to/project"
    exit 1
}

# 檢查參數
if [ -z "$FROM_NODE" ] || [ -z "$TO_NODE" ]; then
    echo -e "${RED}❌ Error: Both from-node and to-node are required${NC}"
    usage
fi

# 解析絕對路徑
PROJECT_PATH=$(cd "$PROJECT_PATH" 2>/dev/null && pwd || echo "$PROJECT_PATH")
WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"
CURRENT_FILE="$WORKSPACE/current-process.json"

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  Node Transition Protocol (NTP)${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Transition: ${CYAN}$FROM_NODE${NC} → ${CYAN}$TO_NODE${NC}"
echo ""

# 檢查工作空間
if [ ! -d "$WORKSPACE" ]; then
    echo -e "${RED}❌ Error: Workspace not found at $WORKSPACE${NC}"
    echo "Run init-cor-afp-ntp.sh first."
    exit 1
fi

if [ ! -f "$CURRENT_FILE" ]; then
    echo -e "${RED}❌ Error: current-process.json not found${NC}"
    exit 1
fi

# 檢查 jq
if ! command -v jq >/dev/null 2>&1; then
    echo -e "${RED}❌ Error: jq is required but not installed${NC}"
    echo "Install with: brew install jq"
    exit 1
fi

# Step 1: 執行 Exit Validation
echo -e "${YELLOW}🔍 Step 1: Running exit validation for $FROM_NODE...${NC}"

EXIT_SCRIPT="$WORKSPACE/$FROM_NODE/exit.sh"
if [ -f "$EXIT_SCRIPT" ]; then
    if bash "$EXIT_SCRIPT"; then
        echo -e "${GREEN}✅ Exit validation passed${NC}"
    else
        echo -e "${RED}❌ Exit validation failed. Cannot proceed.${NC}"
        echo ""
        echo "Please fix the issues and try again."
        exit 1
    fi
else
    echo -e "${YELLOW}⚠️  Warning: No exit.sh found for $FROM_NODE${NC}"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Step 2: 生成 Phase Summary
echo -e "${YELLOW}📝 Step 2: Generating phase summary...${NC}"

TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%S+08:00")
SUMMARY_FILE="$WORKSPACE/phase-summary.md"
TASK_NAME=$(jq -r '.task_name' "$CURRENT_FILE" 2>/dev/null || echo "Unknown")

# 生成節點輸出摘要
NODE_OUTPUTS=$(jq -r ".node_outputs[\"$FROM_NODE\"] // {}" "$CURRENT_FILE" 2>/dev/null || echo "{}")

# 更新 phase-summary.md
cat > "$SUMMARY_FILE" << EOF
## Completed: $FROM_NODE

**Timestamp**: $TIMESTAMP  
**Task**: $TASK_NAME

### 節點輸出
EOF

# 添加節點輸出
if [ "$NODE_OUTPUTS" != "{}" ] && [ "$NODE_OUTPUTS" != "null" ]; then
    echo "$NODE_OUTPUTS" | jq -r 'to_entries | .[] | "- **\(.key)**: \(.value)"' >> "$SUMMARY_FILE" 2>/dev/null || echo "- (No outputs recorded)" >> "$SUMMARY_FILE"
else
    echo "- (No outputs recorded)" >> "$SUMMARY_FILE"
fi

# 添加驗證狀態
cat >> "$SUMMARY_FILE" << EOF

### 驗證狀態
- **Status**: ✅ PASSED
- **Time**: $TIMESTAMP
- **Validation**: Exit checks completed

---

## Next: $TO_NODE

EOF

# 如果下一節點的 README 存在，添加預覽
if [ -f "$WORKSPACE/$TO_NODE/README.md" ]; then
    echo "### Node README Preview" >> "$SUMMARY_FILE"
    echo "" >> "$SUMMARY_FILE"
    echo "\`\`\`markdown" >> "$SUMMARY_FILE"
    head -30 "$WORKSPACE/$TO_NODE/README.md" >> "$SUMMARY_FILE"
    echo "\`\`\`" >> "$SUMMARY_FILE"
    echo "" >> "$SUMMARY_FILE"
fi

echo "**Action**: Read \`$WORKSPACE/$TO_NODE/README.md\` and continue execution." >> "$SUMMARY_FILE"

echo -e "${GREEN}✅ Phase summary saved to $SUMMARY_FILE${NC}"

# Step 3: 更新 current-process.json
echo -e "${YELLOW}💾 Step 3: Updating current-process.json...${NC}"

TMP_FILE=$(mktemp)

jq --arg from "$FROM_NODE" \
   --arg to "$TO_NODE" \
   --arg time "$TIMESTAMP" \
   '
   .completed_nodes |= (. + [$from] | unique) |
   .current_node = $to |
   .updated_at = $time |
   .validation_state[$from] = {
     "passed": true,
     "timestamp": $time,
     "checks": ["exit_validation_passed"]
   } |
   .recovery_hints.last_action = "Completed \($from), transitioning to \($to)"
   ' "$CURRENT_FILE" > "$TMP_FILE"

# 更新 validation-chain.json
VALIDATION_CHAIN="$WORKSPACE/validation-chain.json"
if [ -f "$VALIDATION_CHAIN" ]; then
    jq --arg node "$FROM_NODE" \
       --arg time "$TIMESTAMP" \
       '.chain += [{"node": $node, "result": "PASSED", "timestamp": $time}] | 
        .last_valid_checkpoint = $node' \
       "$VALIDATION_CHAIN" > "${VALIDATION_CHAIN}.tmp" && \
    mv "${VALIDATION_CHAIN}.tmp" "$VALIDATION_CHAIN"
fi

# 移動更新後的檔案
mv "$TMP_FILE" "$CURRENT_FILE"

echo -e "${GREEN}✅ State updated${NC}"

# Step 4: 輸出 Context Compact Point
echo ""
echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║           CONTEXT COMPACT POINT                             ║${NC}"
echo -e "${CYAN}║           (Preserve this information)                       ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}PHASE TRANSITION${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo -e "${GREEN}✅ Completed:${NC} $FROM_NODE"
echo -e "${YELLOW}➡️  Next:${NC} $TO_NODE"
echo -e "${BLUE}🕐 Timestamp:${NC} $TIMESTAMP"
echo ""
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}INSTRUCTIONS FOR AI${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "1. Read the next node README:"
echo -e "   ${CYAN}cat $WORKSPACE/$TO_NODE/README.md${NC}"
echo ""
echo "2. Follow the checklist and execute tasks"
echo ""
echo "3. When done, run exit validation:"
echo -e "   ${CYAN}bash $WORKSPACE/$TO_NODE/exit.sh${NC}"
echo ""
echo "4. Transition to next node:"
echo -e "   ${CYAN}bash .cor-protocol/scripts/node-transition.sh $TO_NODE [next-node]${NC}"
echo ""
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Step 5: 顯示下一節點的 README
echo -e "${YELLOW}📖 Next Node README:${NC}"
echo ""

if [ -f "$WORKSPACE/$TO_NODE/README.md" ]; then
    head -40 "$WORKSPACE/$TO_NODE/README.md"
    echo ""
    echo "... (see full README for complete instructions)"
else
    echo -e "${YELLOW}⚠️  Note: README.md not found for $TO_NODE${NC}"
    echo "Create it at: $WORKSPACE/$TO_NODE/README.md"
fi

echo ""
echo -e "${GREEN}✅ Transition complete: $FROM_NODE → $TO_NODE${NC}"
echo ""
echo -e "${BLUE}Current State:${NC}"
echo "  Session: $(jq -r '.session_id' "$CURRENT_FILE" | cut -d'-' -f1-4)"
echo "  Node: $(jq -r '.current_node' "$CURRENT_FILE")"
echo "  Completed: $(jq -r '.completed_nodes | length' "$CURRENT_FILE") nodes"
echo ""
