#!/bin/bash
#
# 00-init Exit Validation Script
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(dirname "$SCRIPT_DIR")"
CURRENT_NODE="00-init"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  NTP Exit Validation: $CURRENT_NODE${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

ERRORS=()
WARNINGS=()

# Check 1: requirements.md
if [ ! -f "$WORKSPACE/$CURRENT_NODE/requirements.md" ]; then
    ERRORS+=("❌ requirements.md not found")
else
    echo -e "${GREEN}✅ requirements.md exists${NC}"
fi

# Check 2: target-files.json
if [ ! -f "$WORKSPACE/$CURRENT_NODE/target-files.json" ]; then
    ERRORS+=("❌ target-files.json not found")
else
    echo -e "${GREEN}✅ target-files.json exists${NC}"
    
    # Validate JSON structure
    if ! jq empty "$WORKSPACE/$CURRENT_NODE/target-files.json" 2>/dev/null; then
        ERRORS+=("❌ target-files.json is invalid JSON")
    else
        echo -e "${GREEN}✅ target-files.json is valid JSON${NC}"
    fi
fi

# Check 3: references.md (optional but recommended)
if [ ! -f "$WORKSPACE/$CURRENT_NODE/references.md" ]; then
    WARNINGS+=("⚠️  references.md not found (optional but recommended)")
else
    echo -e "${GREEN}✅ references.md exists${NC}"
fi

# Output results
echo ""
if [ ${#ERRORS[@]} -eq 0 ] && [ ${#WARNINGS[@]} -eq 0 ]; then
    echo -e "${GREEN}✅ All validation checks passed!${NC}"
    echo ""
    echo "Ready to transition to next node."
    echo "Run: bash .cor-protocol/scripts/node-transition.sh 00-init 01-analysis"
    exit 0
else
    if [ ${#ERRORS[@]} -gt 0 ]; then
        echo -e "${RED}❌ Validation failed with ${#ERRORS[@]} error(s):${NC}"
        printf '%s\n' "${ERRORS[@]}"
    fi
    
    if [ ${#WARNINGS[@]} -gt 0 ]; then
        echo ""
        echo -e "${YELLOW}⚠️  Warnings (${#WARNINGS[@]}):${NC}"
        printf '%s\n' "${WARNINGS[@]}"
    fi
    
    echo ""
    echo "Please fix the errors before proceeding."
    exit 1
fi
