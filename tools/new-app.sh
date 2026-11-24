#!/usr/bin/env bash
# Start a new firmware product from this repo's full layered skeleton.
#
#   tools/new-app.sh sleep-logger ~/Documents/projects/sleep-logger
#
# Copies the committed tree (git archive HEAD), so the product keeps AGENTS.md, CLAUDE.md,
# both targets, Tests/, the Jenkinsfile, the RAM/flash budget and the arch-qube gate.
# Copying only Shared/ or one service into a fresh CubeMX project loses all of that
# (AGENTS.md §1).
#
# --no-demo is NOT offered, on purpose. In arcana-esp32 the demos sit behind a clean fence;
# here they do not: Mqtt/Ble/AtsStorage consume the Sensor/Light outputs, MainView/MainViewModel
# render every service, OtaService rides on the ESP8266 transport, and 40+ host tests link them
# together. Any automatic strip either breaks the build or leaves dead, still-compiled code —
# the drift that --no-demo exists to prevent. Remove a service you do not need by hand:
# its AppContainer lines, its files, its test (AGENTS.md §1), then run the §4 checks.
set -euo pipefail

if [ "${1:-}" = "--no-demo" ]; then
    echo "new-app: --no-demo is not supported in this template (demo services are wired into MQTT/BLE/storage/view; see the header of $0). Create the app, then remove services via AppContainer." >&2
    exit 1
fi
[ $# -eq 2 ] || { echo "usage: $0 <app-name> <dest-dir>" >&2; exit 1; }
APP="$1"
DEST="$2"
[[ "$APP" =~ ^[a-z][a-z0-9-]*$ ]] || { echo "app-name: lower-case letters, digits or - (used in docker image names)" >&2; exit 1; }
[ ! -e "$DEST" ] || { echo "$DEST already exists — refusing to overwrite" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
git -C "$ROOT" diff --quiet HEAD -- AGENTS.md tools/ || echo "note: uncommitted changes in AGENTS.md/tools are NOT copied (git archive HEAD)" >&2

mkdir -p "$DEST"
git -C "$ROOT" archive HEAD | tar -x -C "$DEST"
SRC_SHA="$(git -C "$ROOT" rev-parse --short HEAD)"
cd "$DEST"

# Product identity — the only places the template name is load-bearing. The ELF names
# (arcana-f051 / arcana-f103) are target names used by tools/ram-budget.conf; they stay.
sed -i.bak -E "s/^project\\(arcana-embedded-stm32 /project(${APP} /" CMakeLists.txt
sed -i.bak -E "s/^sonar.projectKey=.*/sonar.projectKey=${APP}/; s/^sonar.projectName=.*/sonar.projectName=${APP}/" sonar-project.properties
# Jenkinsfile + compose: APP_NAME/PROJECT_NAME, Sonar key, firmware image, test image and the
# per-build container names (two products on one Jenkins must not share them).
sed -i.bak -E \
    -e "s/stm32-app/${APP}/g" \
    -e "s/stm32-test/${APP}-test/g" \
    -e "s/stm32-cov-/${APP}-cov-/g" \
    -e "s/arcana-arch-qube-stm32-/arcana-arch-qube-${APP}-/g" \
    -e "s/(arch-qube-metrics\\.sh [^ ]+ )arcana-embedded-stm32/\\1${APP}/" \
    Jenkinsfile docker-compose.ci.yml
rm -f CMakeLists.txt.bak sonar-project.properties.bak Jenkinsfile.bak docker-compose.ci.yml.bak
grep -q "projectKey=${APP}" Jenkinsfile || { echo "new-app: Jenkinsfile Sonar key not renamed (template drift)" >&2; exit 1; }
grep -q "^project(${APP} " CMakeLists.txt || { echo "new-app: CMake project not renamed (template drift)" >&2; exit 1; }

rm -f CHANGELOG.md .release-please-manifest.json
printf '{\n  ".": "0.1.0"\n}\n' > .release-please-manifest.json

cat >> AGENTS.md <<EOF

## Origin

Created from arcana-embedded-stm32 @ \`${SRC_SHA}\` by \`tools/new-app.sh\`. The layers, rules
and definition of done above apply unchanged. Remove template services you do not need via
AppContainer (and their tests), not by flattening.
EOF

git init -q
tools/install-hooks.sh >/dev/null
git add -A
# --no-verify only for this import commit: every file counts as "added" here, including the
# template's legacy flat files (Shared/Src/uECC.c, Shared/Inc/App.hpp). The template itself
# passes arch-check; every later commit in the new product goes through the hook.
git -c user.name="${GIT_AUTHOR_NAME:-new-app}" -c user.email="${GIT_AUTHOR_EMAIL:-new-app@localhost}" \
    commit -q --no-verify -m "chore: start ${APP} from arcana-embedded-stm32 ${SRC_SHA}"

echo "created $DEST (${APP}) from arcana-embedded-stm32 ${SRC_SHA}"
echo "next: cd $DEST && tools/new-service.sh <Name>   # add capabilities as services"
