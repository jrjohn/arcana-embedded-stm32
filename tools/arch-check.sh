#!/usr/bin/env bash
# Run the same arch-qube scan CI runs (Jenkinsfile stage "Architecture Qube").
# Exit 0 = pass. Exit 2 = could not check (NOT a pass). Anything else = not done (AGENTS.md §5).
#
#   tools/arch-check.sh
#   ARCH_QUBE_IMAGE=... tools/arch-check.sh      # override the image (e.g. a local build)
#
# Image: tools/arch-qube-image.sh (version in tools/arch-qube.version; ghcr.io, then
# arcana.boo; never :latest). No Docker? See AGENTS.md "Architecture check (arch-qube)".
# Streams the source in with tar | docker cp (a bind mount is empty under Docker-in-Docker)
# and writes the report to arch-qube-reports/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THRESHOLD=90          # keep equal to the Jenkinsfile stage "Architecture Qube" (--threshold 90)
FRAMEWORK=stm32       # keep equal to the Jenkinsfile (--framework stm32)
NAME="arch-check-$$"

command -v docker >/dev/null || { echo "arch-check: docker not found — cannot check (this is NOT a pass)" >&2; exit 2; }
docker info >/dev/null 2>&1 || { echo "arch-check: docker daemon not reachable — cannot check (this is NOT a pass)" >&2; exit 2; }
IMAGE="$("$ROOT/tools/arch-qube-image.sh")" || exit 2

cleanup() { docker rm -fv "$NAME" >/dev/null 2>&1 || true; }
trap cleanup EXIT
echo "arch-check: image $IMAGE"

docker create --name "$NAME" -v /src -v /output "$IMAGE" \
    scan /src --framework "$FRAMEWORK" --no-ai --ci \
    --format json,markdown -o /output --threshold "$THRESHOLD" >/dev/null \
    || { echo "arch-check: could not create the arch-qube container ($IMAGE) — NOT a pass" >&2; exit 2; }

# macOS bsdtar carries extended attributes (com.apple.provenance) that docker cp refuses.
if tar --version 2>/dev/null | grep -q bsdtar; then
    TAR_NOXATTR=(--no-xattrs --no-mac-metadata)
    export COPYFILE_DISABLE=1
else
    TAR_NOXATTR=(--no-xattrs)
fi
# Same exclusions as the Jenkinsfile stage "Architecture Qube".
tar "${TAR_NOXATTR[@]}" --exclude=./.git --exclude=./build --exclude=./Tests/build \
    --exclude=./arch-qube-reports --exclude=./.scannerwork \
    -C "$ROOT" -cf - . | docker cp - "$NAME":/src

set +e
docker start -a "$NAME"
RC=$?
set -e

mkdir -p "$ROOT/arch-qube-reports"
docker cp "$NAME":/output/. "$ROOT/arch-qube-reports/" >/dev/null 2>&1 || true

if [ "$RC" -eq 0 ]; then
    echo "arch-check: PASS ($IMAGE; report: arch-qube-reports/)"
else
    echo "arch-check: FAIL rc=$RC — --ci prints nothing on failure: read arch-qube-reports/arch-qube.md. Fix the structure; never lower the threshold (AGENTS.md §5)" >&2
fi
exit "$RC"
