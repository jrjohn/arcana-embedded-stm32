#!/usr/bin/env bash
# Print the arch-qube image pinned by tools/arch-qube.version.
# Tried in order: ghcr.io/jrjohn/arch-qube:<ver> (public, multi-arch), then
# arcana.boo/arcana/arch-qube:<ver>. ARCH_QUBE_IMAGE overrides (e.g. a local build).
# Neither available -> exit 2 ("NOT a pass"). There is deliberately NO fallback to :latest:
# an unpinned image silently changed arcana-embedded-esp32's CI verdict without a commit.
# Used by tools/arch-check.sh AND the Jenkinsfile, so local and CI scan with the same image.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/tools/arch-qube.version")"

for cand in ${ARCH_QUBE_IMAGE:-} "ghcr.io/jrjohn/arch-qube:${VERSION}" "arcana.boo/arcana/arch-qube:${VERSION}"; do
    if docker image inspect "$cand" >/dev/null 2>&1 || docker pull -q "$cand" >/dev/null 2>&1; then
        echo "$cand"
        exit 0
    fi
done
echo "arch-qube: ${VERSION} not available from ghcr.io or arcana.boo — cannot check (this is NOT a pass)" >&2
exit 2
