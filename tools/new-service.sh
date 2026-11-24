#!/usr/bin/env bash
# Scaffold a layered F103 service: interface + impl/ + Input/Output structs + AppContainer
# wiring + host GoogleTest registered in Tests/CMakeLists.txt.
#
#   tools/new-service.sh Pairing    # -> PairingService / PairingServiceImpl, namespace arcana::pairing
#
# Firmware CMake needs no edit: CMakeLists.txt globs Targets/stm32f103ze/Main/*.cpp and already
# has service/ and service/impl/ on the include path. The generated service subscribes to the
# Timer's BaseTimer (Input) and publishes <Name>Model (Output) — replace the body, keep the shape.
# Fails loudly (exit 1) if an anchor it edits is missing; it never guesses a place, and it
# checks every anchor before writing anything.
set -euo pipefail

[ $# -eq 1 ] || { echo "usage: $0 <Name>   (PascalCase, e.g. Pairing)" >&2; exit 1; }
NAME="$1"
[[ "$NAME" =~ ^[A-Z][A-Za-z0-9]*$ ]] || { echo "Name must be PascalCase (e.g. Pairing)" >&2; exit 1; }
if [[ "$NAME" =~ Service$ ]]; then echo "give the name without the 'Service' suffix" >&2; exit 1; fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAIN="$ROOT/Targets/stm32f103ze/Main"
SVC="$MAIN/service"
NS="$(printf '%s' "$NAME" | tr '[:upper:]' '[:lower:]')"
LOWER="$(printf '%s' "$NAME" | sed -E 's/([a-z0-9])([A-Z])/\1_\2/g' | tr '[:upper:]' '[:lower:]')"
IFACE="$SVC/${NAME}Service.hpp"
IMPL_H="$SVC/impl/${NAME}ServiceImpl.hpp"
IMPL_C="$SVC/impl/${NAME}ServiceImpl.cpp"
TEST="$ROOT/Tests/test_${LOWER}_service.cpp"
AC_H="$MAIN/AppContainer.hpp"
AC_C="$MAIN/AppContainer.cpp"
TCM="$ROOT/Tests/CMakeLists.txt"

for f in "$IFACE" "$IMPL_H" "$IMPL_C" "$TEST"; do
    [ ! -e "$f" ] || { echo "exists: ${f#"$ROOT"/} — refusing to overwrite" >&2; exit 1; }
done
need() {
    grep -q -- "$2" "$1" || {
        echo "new-service: anchor '$2' missing in ${1#"$ROOT"/} — restore it (AGENTS.md §3); nothing was changed" >&2
        exit 1
    }
}
need "$AC_H" '// new-service:interfaces '
need "$AC_H" '// new-service:members '
for a in impls wire initHAL init start; do need "$AC_C" "// new-service:$a "; done
need "$TCM" '^set(F103_SVC_IMPL '
need "$TCM" '^add_test(NAME test_services_f103 '

cat > "$IFACE" <<EOF
#pragma once

#include "Observable.hpp"
#include "Models.hpp"
#include "ServiceTypes.hpp"

namespace arcana {
namespace ${NS} {

// Value published by ${NAME}Service. Observable<T> queues a POINTER, so the instance
// published must outlive dispatch (a member of the impl, never a stack local).
class ${NAME}Model : public Model {
public:
    uint32_t count = 0;   // TODO: replace with the real fields
    ${NAME}Model() : Model(static_cast<uint8_t>(ModelType::User)) {}
};

class ${NAME}Service {
public:
    struct Input {
        Observable<TimerModel>* TimerEvents;   // wired by AppContainer::wireServices()
    };
    struct Output {
        Observable<${NAME}Model>* StateEvents;
    };

    Input input;
    Output output;

    virtual ~${NAME}Service() {}
    virtual ServiceStatus initHAL() = 0;
    virtual ServiceStatus init() = 0;
    virtual ServiceStatus start() = 0;
    virtual void stop() = 0;

protected:
    ${NAME}Service() : input(), output() {
        input.TimerEvents = 0;
        output.StateEvents = 0;
    }
};

} // namespace ${NS}
} // namespace arcana
EOF

cat > "$IMPL_H" <<EOF
#pragma once

#include "${NAME}Service.hpp"

namespace arcana {
namespace ${NS} {

class ${NAME}ServiceImpl : public ${NAME}Service {
public:
    static ${NAME}Service& getInstance();

    ServiceStatus initHAL() override;
    ServiceStatus init() override;
    ServiceStatus start() override;
    void stop() override;

    const ${NAME}Model& state() const { return mModel; }

private:
    ${NAME}ServiceImpl();
    ~${NAME}ServiceImpl();
    ${NAME}ServiceImpl(const ${NAME}ServiceImpl&);
    ${NAME}ServiceImpl& operator=(const ${NAME}ServiceImpl&);

    static void onTimerTick(TimerModel* model, void* context);

    Observable<${NAME}Model> mState;   // static storage via the singleton — no heap
    ${NAME}Model mModel;
    bool mRunning;
};

} // namespace ${NS}
} // namespace arcana
EOF

cat > "$IMPL_C" <<EOF
#include "${NAME}ServiceImpl.hpp"

namespace arcana {
namespace ${NS} {

${NAME}ServiceImpl::${NAME}ServiceImpl()
    : mState("${NAME}")
    , mModel()
    , mRunning(false)
{
    output.StateEvents = &mState;
}

${NAME}ServiceImpl::~${NAME}ServiceImpl() {}

${NAME}Service& ${NAME}ServiceImpl::getInstance() {
    static ${NAME}ServiceImpl sInstance;
    return sInstance;
}

ServiceStatus ${NAME}ServiceImpl::initHAL() { return ServiceStatus::OK; }

ServiceStatus ${NAME}ServiceImpl::init() {
    if (!input.TimerEvents) return ServiceStatus::InvalidState;
    input.TimerEvents->subscribe(onTimerTick, this);
    return ServiceStatus::OK;
}

ServiceStatus ${NAME}ServiceImpl::start() {
    mRunning = true;
    return ServiceStatus::OK;
}

void ${NAME}ServiceImpl::stop() { mRunning = false; }

// Runs on the Observable dispatcher task, not in an ISR (from an ISR use publishFromISR).
void ${NAME}ServiceImpl::onTimerTick(TimerModel* /*model*/, void* context) {
    ${NAME}ServiceImpl* self = static_cast<${NAME}ServiceImpl*>(context);
    if (!self->mRunning) return;
    self->mModel.count++;
    self->mModel.updateTimestamp();
    self->mState.publish(&self->mModel);
}

} // namespace ${NS}
} // namespace arcana
EOF

cat > "$TEST" <<EOF
/**
 * @file test_${LOWER}_service.cpp
 * @brief Host test for ${NAME}ServiceImpl (scaffolded by tools/new-service.sh).
 */
#include <gtest/gtest.h>

#include "FreeRTOS.h"
#include "${NAME}ServiceImpl.hpp"

using arcana::Observable;
using arcana::ServiceStatus;
using arcana::TimerModel;
using arcana::${NS}::${NAME}ServiceImpl;

static ${NAME}ServiceImpl& svc() {
    return static_cast<${NAME}ServiceImpl&>(${NAME}ServiceImpl::getInstance());
}

TEST(${NAME}ServiceTest, InitWithoutTimerInputIsInvalidState) {
    svc().input.TimerEvents = nullptr;
    EXPECT_EQ(svc().initHAL(), ServiceStatus::OK);
    EXPECT_EQ(svc().init(), ServiceStatus::InvalidState);
}

TEST(${NAME}ServiceTest, ExposesStateObservable) {
    EXPECT_NE(svc().output.StateEvents, nullptr);
}

TEST(${NAME}ServiceTest, TimerTickUpdatesStateOnlyWhileRunning) {
    Observable<TimerModel> timer("t");
    TimerModel tick;
    svc().input.TimerEvents = &timer;
    ASSERT_EQ(svc().init(), ServiceStatus::OK);

    const uint32_t before = svc().state().count;
    timer.notify(&tick);                       // not started: ignored
    EXPECT_EQ(svc().state().count, before);

    ASSERT_EQ(svc().start(), ServiceStatus::OK);
    timer.notify(&tick);
    EXPECT_EQ(svc().state().count, before + 1);

    svc().stop();
    timer.notify(&tick);
    EXPECT_EQ(svc().state().count, before + 1);
}
EOF

# AppContainer: the only file that sees *Impl (AGENTS.md R2). Keeps the file's line endings.
ins() {   # ins <file> <anchor> <line...>: insert the lines above the anchor line
    local file="$1" anchor="$2"; shift 2
    BLOCK="$(printf '%s\n' "$@")" awk -v a="$anchor" '
        index($0, a) {
            cr = (substr($0, length($0)) == "\r") ? "\r" : ""
            n = split(ENVIRON["BLOCK"], L, "\n")
            for (i = 1; i <= n; i++) print L[i] cr
        }
        { print }' "$file" > "$file.tmp"
    mv "$file.tmp" "$file"
}
ins "$AC_H" '// new-service:interfaces ' "#include \"${NAME}Service.hpp\""
ins "$AC_H" '// new-service:members '    "    ${NS}::${NAME}Service* m${NAME} = nullptr;"
ins "$AC_C" '// new-service:impls '      "#include \"${NAME}ServiceImpl.hpp\""
ins "$AC_C" '// new-service:wire '       "    m${NAME} = &${NS}::${NAME}ServiceImpl::getInstance();" \
                                          "    m${NAME}->input.TimerEvents = mTimer->output.BaseTimer;"
ins "$AC_C" '// new-service:initHAL '    "    m${NAME}->initHAL();"
ins "$AC_C" '// new-service:init '       "    m${NAME}->init();"
ins "$AC_C" '// new-service:start '      "    m${NAME}->start();"

# Tests/CMakeLists.txt: one executable per service, same shape as test_timer_service.
cat >> "$TCM" <<EOF

# ── test_${LOWER}_service (scaffolded by tools/new-service.sh) ─────────────────
add_executable(test_${LOWER}_service
    test_${LOWER}_service.cpp
    \${F103_SVC_IMPL}/${NAME}ServiceImpl.cpp
    \${SHARED_CORE_EVENT}/Observable.cpp
    \${FREERTOS_STUBS}
)
target_include_directories(test_${LOWER}_service PRIVATE
    \${COMMON_INCS} \${F103_COMMON} \${F103_SVC_INC} \${F103_SVC_IMPL} \${F103_MODEL})
target_link_libraries(test_${LOWER}_service PRIVATE GTest::gtest_main)
add_test(NAME test_${LOWER}_service COMMAND test_${LOWER}_service)
EOF

echo "created:"
for f in "$IFACE" "$IMPL_H" "$IMPL_C" "$TEST"; do echo "  ${f#"$ROOT"/}"; done
echo "updated: Targets/stm32f103ze/Main/AppContainer.{hpp,cpp}, Tests/CMakeLists.txt"
echo "next: replace the TODO model/logic (keep the shape), then run the AGENTS.md §4 checks"
