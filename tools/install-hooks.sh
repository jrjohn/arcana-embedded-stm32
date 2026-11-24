#!/usr/bin/env bash
# Point git at tools/githooks so the pre-commit architecture check runs on every commit.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$ROOT" config core.hooksPath tools/githooks
echo "hooks installed: core.hooksPath=tools/githooks"
