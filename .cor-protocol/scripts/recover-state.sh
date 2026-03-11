#!/bin/bash
#
# recover-state.sh
# AFP (Anti-Forgetting Protocol) - Compaction 後狀態恢復
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
PROJECT_PATH="${1:-.}"

# 解析絕對路徑
PROJECT_PATH=$(cd "$PROJECT_PATH" 2>/dev/null && pwd || echo "$PROJECT_PATH")
WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  AFP State Recovery${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Project: ${CYAN}$PROJECT_PATH${NC}"
echo ""

# 檢查工作空間
if [ ! -d "$WORKSPACE" ]; then
    echo -e "${RED}❌ Error: Workspace not found at $WORKSPACE${NC}"
    echo ""
    echo -e "${YELLOW}Cannot recover state. Workspace is missing.${NC}"
    echo ""
    echo "Options:"
    echo "   1. Check if you're in the correct project directory"
    echo "   2. Initialize a new workspace:"
    echo "      bash .cor-protocol/scripts/init-cor-afp-ntp.sh 'task-name'"
    exit 1
fi

# Step 1: 執行健康檢查
echo -e "${YELLOW}🔍 Step 1: Running health check...${NC}"
echo ""

HEALTH_PASSED=true
bash "$WORKSPACE/../.cor-protocol/scripts/quick-health-check.sh" "$PROJECT_PATH" 2>/dev/null || HEALTH_PASSED=false

echo ""

# Step 2: 讀取 Phase Summary
echo -e "${YELLOW}📋 Step 2: Loading phase summary...${NC}"
echo ""

if [ -f "$WORKSPACE/phase-summary.md" ]; then
    echo -e "${BLUE}─────────────────────────────────────────────────────────────${NC}"
    cat "$WORKSPACE/phase-summary.md"
    echo -e "${BLUE}─────────────────────────────────────────────────────────────${NC}"
    echo ""
else
    echo -e "${YELLOW}⚠️  Phase summary not found${NC}"
    echo ""
fi

# Step 3: 讀取 Current State
echo -e "${YELLOW}📊 Step 3: Loading current state...${NC}"
echo ""

if [ -f "$WORKSPACE/current-process.json" ]; then
    if command -v jq >/dev/null 2>&1; then
        echo -e "${BLUE}─────────────────────────────────────────────────────────────${NC}"
        
        # 提取關鍵資訊
        SESSION_ID=$(jq -r '.session_id' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
        TASK_NAME=$(jq -r '.task_name' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
        CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
        STARTED_AT=$(jq -r '.started_at' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
        COMPLETED=$(jq -r '.completed_nodes | join(", ")' "$WORKSPACE/current-process.json" 2>/dev/null || echo "None")
        LAST_ACTION=$(jq -r '.recovery_hints.last_action' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
        
        echo -e "${CYAN}Session:${NC} ${SESSION_ID:0:8}..."
        echo -e "${CYAN}Task:${NC} $TASK_NAME"
        echo -e "${CYAN}Current Node:${NC} $CURRENT_NODE"
        echo -e "${CYAN}Started:${NC} $STARTED_AT"
        echo -e "${CYAN}Completed Nodes:${NC} $COMPLETED"
        echo -e "${CYAN}Last Action:${NC} $LAST_ACTION"
        
        # 顯示關鍵決策
        echo ""
        echo -e "${CYAN}Critical Decisions:${NC}"
        jq -r '.recovery_hints.critical_decisions[]? // empty' "$WORKSPACE/current-process.json" 2>/dev/null | while read -r decision; do
            echo "   • $decision"
        done || echo "   (None recorded)"
        
        echo -e "${BLUE}─────────────────────────────────────────────────────────────${NC}"
    else
        echo -e "${YELLOW}⚠️  jq not available, showing raw JSON (first 20 lines):${NC}"
        head -20 "$WORKSPACE/current-process.json"
    fi
    echo ""
else
    echo -e "${RED}❌ current-process.json not found${NC}"
    echo ""
fi

# Step 4: 檢查 Validation Chain
echo -e "${YELLOW}🔗 Step 4: Loading validation chain...${NC}"
echo ""

if [ -f "$WORKSPACE/validation-chain.json" ]; then
    if command -v jq >/dev/null 2>&1; then
        CHAIN_LENGTH=$(jq '.chain | length' "$WORKSPACE/validation-chain.json")
        LAST_CHECKPOINT=$(jq -r '.last_valid_checkpoint' "$WORKSPACE/validation-chain.json")
        
        echo -e "${GREEN}✅ Validation chain found${NC}"
        echo -e "   Chain length: $CHAIN_LENGTH"
        echo -e "   Last checkpoint: $LAST_CHECKPOINT"
        
        if [ "$CHAIN_LENGTH" -gt 0 ]; then
            echo ""
            echo -e "${BLUE}Validation History:${NC}"
            jq -r '.chain[] | "   \(.node): \(.result) at \(.timestamp)"' "$WORKSPACE/validation-chain.json"
        fi
    fi
    echo ""
else
    echo -e "${YELLOW}⚠️  Validation chain not found${NC}"
    echo ""
fi

# Step 5: 輸出恢復指引
echo -e "${YELLOW}📝 Step 5: Recovery instructions...${NC}"
echo ""

echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║           RECOVERY INSTRUCTIONS                             ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ -f "$WORKSPACE/current-process.json" ]; then
    CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json" 2>/dev/null || echo "")
    
    if [ -n "$CURRENT_NODE" ] && [ "$CURRENT_NODE" != "null" ]; then
        echo -e "${GREEN}1. Continue from current node:${NC} ${YELLOW}$CURRENT_NODE${NC}"
        echo ""
        echo -e "   Read the node README:"
        echo -e "   ${CYAN}cat .cor-afp-ntp/$CURRENT_NODE/README.md${NC}"
        echo ""
        
        # 如果 README 存在，顯示預覽
        if [ -f "$WORKSPACE/$CURRENT_NODE/README.md" ]; then
            echo -e "   ${BLUE}Node README Preview:${NC}"
            echo -e "   ${BLUE}────────────────────────────────────────────────────────${NC}"
            head -15 "$WORKSPACE/$CURRENT_NODE/README.md" | sed 's/^/   /'
            echo -e "   ${BLUE}────────────────────────────────────────────────────────${NC}"
            echo ""
        fi
        
        echo -e "2. Review the checklist:"
        if [ -f "$WORKSPACE/$CURRENT_NODE/checklist.md" ]; then
            echo -e "   ${CYAN}cat .cor-afp-ntp/$CURRENT_NODE/checklist.md${NC}"
        else
            echo -e "   ${YELLOW}(No checklist found)${NC}"
        fi
        echo ""
        
        echo -e "3. After completing tasks, run:"
        echo -e "   ${CYAN}bash .cor-afp-ntp/$CURRENT_NODE/exit.sh${NC}"
        echo ""
        
        echo -e "4. Transition to next node:"
        echo -e "   ${CYAN}bash .cor-protocol/scripts/node-transition.sh $CURRENT_NODE [next-node]${NC}"
    else
        echo -e "${YELLOW}⚠️  Cannot determine current node${NC}"
        echo "You may need to manually inspect the workspace and restart."
    fi
else
    echo -e "${RED}❌ No state file found.${NC}"
    echo ""
    echo -e "${YELLOW}Recommended actions:${NC}"
    echo "   1. Check workspace contents: ls -la $WORKSPACE"
    echo "   2. Initialize new workspace:"
    echo "      bash .cor-protocol/scripts/init-cor-afp-ntp.sh 'task-name'"
fi

echo ""
echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║           CRITICAL CONTEXT TO REMEMBER                      ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ -f "$WORKSPACE/current-process.json" ]; then
    # 顯示關鍵上下文
    echo -e "${YELLOW}Task:${NC}"
    jq -r '.task_name' "$WORKSPACE/current-process.json" 2>/dev/null | sed 's/^/   /' || echo "   (Unknown)"
    echo ""
    
    echo -e "${YELLOW}Last Action:${NC}"
    jq -r '.recovery_hints.last_action' "$WORKSPACE/current-process.json" 2>/dev/null | sed 's/^/   /' || echo "   (Unknown)"
    echo ""
    
    echo -e "${YELLOW}Critical Decisions:${NC}"
    CRITICAL_COUNT=$(jq '.recovery_hints.critical_decisions | length' "$WORKSPACE/current-process.json" 2>/dev/null || echo 0)
    if [ "$CRITICAL_COUNT" -gt 0 ]; then
        jq -r '.recovery_hints.critical_decisions[]' "$WORKSPACE/current-process.json" 2>/dev/null | sed 's/^/   • /' || echo "   (None)"
    else
        echo "   (None recorded)"
    fi
    echo ""
    
    echo -e "${YELLOW}Files Modified:${NC}"
    MODIFIED_COUNT=$(jq '.recovery_hints.files_modified | length' "$WORKSPACE/current-process.json" 2>/dev/null || echo 0)
    if [ "$MODIFIED_COUNT" -gt 0 ]; then
        jq -r '.recovery_hints.files_modified[]' "$WORKSPACE/current-process.json" 2>/dev/null | sed 's/^/   - /' || echo "   (None)"
    else
        echo "   (None recorded)"
    fi
fi

echo ""
echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║           CONTEXT COMPACT POINT                             ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ -f "$WORKSPACE/phase-summary.md" ]; then
    # 提取最後一個完成節點的摘要
    echo -e "${BLUE}From phase-summary.md:${NC}"
    echo ""
    grep -A 50 "^## Completed:" "$WORKSPACE/phase-summary.md" | head -30 || echo "   (No summary available)"
fi

echo ""
echo -e "${GREEN}✅ Recovery information loaded.${NC}"
echo -e "${YELLOW}You can now continue execution.${NC}"
echo ""

# 提示檢查是否通過
if [ "$HEALTH_PASSED" = false ]; then
    echo -e "${YELLOW}⚠️  Warning: Health check detected issues.${NC}"
    echo "Review the warnings above before continuing."
fi

echo ""
