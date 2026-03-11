#!/bin/bash
#
# quick-health-check.sh
# AFP (Anti-Forgetting Protocol) - 快速健康檢查
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
VERBOSE="${2:-false}"

# 解析絕對路徑
PROJECT_PATH=$(cd "$PROJECT_PATH" 2>/dev/null && pwd || echo "$PROJECT_PATH")
WORKSPACE="$PROJECT_PATH/.cor-afp-ntp"

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  AFP Quick Health Check${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo -e "Project: ${CYAN}$PROJECT_PATH${NC}"
echo -e "Workspace: ${CYAN}$WORKSPACE${NC}"
echo ""

# 統計
ERRORS=()
WARNINGS=()
PASSED=()

# Check 1: Workspace 目錄存在
echo -e "${YELLOW}🔍 Checking workspace...${NC}"

if [ ! -d "$WORKSPACE" ]; then
    ERRORS+=("❌ Workspace directory not found: $WORKSPACE")
else
    PASSED+=("✅ Workspace directory exists")
    
    # 計算目錄大小
    if [ "$VERBOSE" = "true" ]; then
        SIZE=$(du -sh "$WORKSPACE" 2>/dev/null | cut -f1 || echo "unknown")
        echo "   Size: $SIZE"
    fi
fi

# Check 2: current-process.json
echo -e "${YELLOW}🔍 Checking current-process.json...${NC}"

if [ ! -f "$WORKSPACE/current-process.json" ]; then
    ERRORS+=("❌ current-process.json not found")
else
    PASSED+=("✅ current-process.json exists")
    
    # 驗證 JSON 格式
    if ! jq empty "$WORKSPACE/current-process.json" 2>/dev/null; then
        ERRORS+=("❌ current-process.json contains invalid JSON")
    else
        PASSED+=("✅ current-process.json is valid JSON")
        
        # 檢查必要欄位
        REQUIRED_FIELDS=("session_id" "current_node" "task_name" "started_at")
        for field in "${REQUIRED_FIELDS[@]}"; do
            if ! jq -e ".$field" "$WORKSPACE/current-process.json" >/dev/null 2>&1; then
                ERRORS+=("❌ Missing required field: $field")
            fi
        done
        
        # 顯示當前狀態
        if [ ${#ERRORS[@]} -eq 0 ]; then
            CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json")
            TASK_NAME=$(jq -r '.task_name' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
            COMPLETED_COUNT=$(jq -r '.completed_nodes | length' "$WORKSPACE/current-process.json")
            UPDATED_AT=$(jq -r '.updated_at' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")
            
            echo ""
            echo -e "${CYAN}📊 Current State:${NC}"
            echo -e "   Task: ${GREEN}$TASK_NAME${NC}"
            echo -e "   Current Node: ${YELLOW}$CURRENT_NODE${NC}"
            echo -e "   Completed Nodes: ${GREEN}$COMPLETED_COUNT${NC}"
            echo -e "   Last Updated: $UPDATED_AT"
        fi
    fi
fi

# Check 3: Current node directory
echo -e "${YELLOW}🔍 Checking current node directory...${NC}"

if [ -f "$WORKSPACE/current-process.json" ]; then
    CURRENT_NODE=$(jq -r '.current_node' "$WORKSPACE/current-process.json" 2>/dev/null || echo "")
    
    if [ -n "$CURRENT_NODE" ] && [ "$CURRENT_NODE" != "null" ]; then
        if [ ! -d "$WORKSPACE/$CURRENT_NODE" ]; then
            WARNINGS+=("⚠️  Current node directory not found: $CURRENT_NODE")
        else
            PASSED+=("✅ Current node directory exists: $CURRENT_NODE")
            
            # 檢查 README.md
            if [ ! -f "$WORKSPACE/$CURRENT_NODE/README.md" ]; then
                WARNINGS+=("⚠️  README.md missing in $CURRENT_NODE")
            else
                PASSED+=("✅ README.md exists in $CURRENT_NODE")
            fi
            
            # 檢查 exit.sh
            if [ ! -f "$WORKSPACE/$CURRENT_NODE/exit.sh" ]; then
                WARNINGS+=("⚠️  exit.sh missing in $CURRENT_NODE")
            else
                PASSED+=("✅ exit.sh exists in $CURRENT_NODE")
            fi
        fi
    fi
fi

# Check 4: validation-chain.json
echo -e "${YELLOW}🔍 Checking validation-chain.json...${NC}"

if [ ! -f "$WORKSPACE/validation-chain.json" ]; then
    WARNINGS+=("⚠️  validation-chain.json not found (optional)")
else
    if ! jq empty "$WORKSPACE/validation-chain.json" 2>/dev/null; then
        ERRORS+=("❌ validation-chain.json contains invalid JSON")
    else
        PASSED+=("✅ validation-chain.json is valid")
        
        CHAIN_LENGTH=$(jq '.chain | length' "$WORKSPACE/validation-chain.json")
        echo "   Validation chain length: $CHAIN_LENGTH"
    fi
fi

# Check 5: phase-summary.md
echo -e "${YELLOW}🔍 Checking phase-summary.md...${NC}"

if [ ! -f "$WORKSPACE/phase-summary.md" ]; then
    WARNINGS+=("⚠️  phase-summary.md not found (needed for recovery)")
else
    PASSED+=("✅ phase-summary.md exists")
fi

# Check 6: checkpoints 目錄
echo -e "${YELLOW}🔍 Checking checkpoints...${NC}"

if [ -d "$WORKSPACE/checkpoints" ]; then
    CHECKPOINT_COUNT=$(ls -1 "$WORKSPACE/checkpoints"/*.json 2>/dev/null | wc -l || echo 0)
    if [ "$CHECKPOINT_COUNT" -gt 0 ]; then
        PASSED+=("✅ Checkpoints available: $CHECKPOINT_COUNT")
    else
        WARNINGS+=("⚠️  No checkpoints created yet")
    fi
else
    WARNINGS+=("⚠️  Checkpoints directory not found")
fi

# 輸出結果
echo ""
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  Health Check Results${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

if [ "$VERBOSE" = "true" ]; then
    # 詳細輸出
    echo -e "${GREEN}Passed (${#PASSED[@]}):${NC}"
    for item in "${PASSED[@]}"; do
        echo "   $item"
    done
    echo ""
    
    if [ ${#WARNINGS[@]} -gt 0 ]; then
        echo -e "${YELLOW}Warnings (${#WARNINGS[@]}):${NC}"
        for item in "${WARNINGS[@]}"; do
            echo "   $item"
        done
        echo ""
    fi
    
    if [ ${#ERRORS[@]} -gt 0 ]; then
        echo -e "${RED}Errors (${#ERRORS[@]}):${NC}"
        for item in "${ERRORS[@]}"; do
            echo "   $item"
        done
        echo ""
    fi
else
    # 簡要輸出
    echo -e "${GREEN}✅ Passed: ${#PASSED[@]}${NC}"
    [ ${#WARNINGS[@]} -gt 0 ] && echo -e "${YELLOW}⚠️  Warnings: ${#WARNINGS[@]}${NC}"
    [ ${#ERRORS[@]} -gt 0 ] && echo -e "${RED}❌ Errors: ${#ERRORS[@]}${NC}"
    echo ""
fi

# 結論
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"

if [ ${#ERRORS[@]} -eq 0 ] && [ ${#WARNINGS[@]} -eq 0 ]; then
    echo -e "${GREEN}✅ All checks passed! System is healthy.${NC}"
    echo ""
    echo -e "${CYAN}Current Node:${NC} $(jq -r '.current_node' "$WORKSPACE/current-process.json" 2>/dev/null || echo "Unknown")"
    echo -e "${CYAN}Progress:${NC} $(jq -r '.completed_nodes | length' "$WORKSPACE/current-process.json" 2>/dev/null || echo 0) / 6 nodes"
    exit 0
elif [ ${#ERRORS[@]} -eq 0 ]; then
    echo -e "${YELLOW}⚠️  System is functional with warnings.${NC}"
    echo ""
    echo "Recommended actions:"
    for item in "${WARNINGS[@]}"; do
        echo "   $item"
    done
    exit 0
else
    echo -e "${RED}❌ System has errors that need to be fixed.${NC}"
    echo ""
    echo "Critical issues:"
    for item in "${ERRORS[@]}"; do
        echo "   $item"
    done
    echo ""
    echo -e "${YELLOW}Recovery options:${NC}"
    echo "   1. Run: bash .cor-protocol/scripts/recover-state.sh"
    echo "   2. Or reset: rm -rf $WORKSPACE && re-init"
    exit 1
fi
