# AGENTS.md — arcana-embedded-stm32

Instructions for any AI coding agent (Claude Code, Codex, Cursor, Copilot, Gemini…) working
in this repository **or in any product derived from it**. `CLAUDE.md`, `GEMINI.md` and
`.github/copilot-instructions.md` all point here; this file is the single source.

> **You MUST NOT report a task as done until `tools/arch-check.sh` exits 0 and every
> command in §4 passes.** Violations are blocked twice: the pre-commit hook
> (`tools/githooks/pre-commit`) rejects new sources outside the layer directories and
> `*Impl.hpp` includes outside `AppContainer.cpp`, and Jenkins blocks the merge (stages
> "RAM/Flash Budget", "Unit Tests + Coverage", "Architecture Qube"). Skipping them costs a
> red PR found after the work is written, or a board that links but has no stack left.

## 0. First command in a fresh clone

Run `tools/install-hooks.sh` once. Git does not install hooks on clone; until you run it the
pre-commit check does not exist and only CI catches a flat layout. `tools/new-app.sh` runs it
for you.

## 1. Starting a new product, adding a feature

| Situation | Do this | Never do this |
|---|---|---|
| New firmware product | `tools/new-app.sh <name> <dest-dir>` — copies the whole skeleton **with this file, the hook, the budget and the CI gates** | Copy `Shared/` or one service into a fresh CubeMX project |
| New capability (service / feature) | `tools/new-service.sh <Name>` — interface, `impl/`, Input/Output structs, AppContainer wiring, host test, Tests CMake | Write `Foo.cpp` next to `App.cpp`, or call one `*Impl` from another |
| New peripheral chip | One class in `Targets/stm32f103ze/Main/driver/` | Call `HAL_I2C_*` / `HAL_SPI_*` from a service or view |
| Drop a template service | Delete its AppContainer lines, its files and its test, then run §4 | Leave it "unwired" (it still compiles, still takes RAM) |

`--no-demo` is deliberately not offered: the template's sample services are wired into
MQTT, BLE, storage and the LCD view, so an automatic strip breaks the build or leaves dead
code (reason in the header of `tools/new-app.sh`).

## 2. Layers — where a file goes

Two targets. **arcana-f103** (`Targets/stm32f103ze`, Cortex-M3, 64 KB RAM) carries the full
layered architecture and is where new services go. **arcana-f051** (`Targets/stm32f051c8`,
Cortex-M0, **8 KB RAM**) is the minimal target: concrete services wired directly in
`Main/App.cpp`, no DI container and no interface split, because every vtable and pointer
counts there. `Targets/stm32f103ze-bootloader` is a separate Makefile project, not built by CI.

| Layer | Path | Contains | May depend on |
|---|---|---|---|
| Entry + DI | F103 `Main/App.cpp`, `Main/AppContainer.{hpp,cpp}`; F051 `Main/App.cpp` | `App_Init`, construction, `wireServices()` | everything — the only place allowed to include `*Impl.hpp` |
| View | F103 `Main/view/` (`BaseLcdView`, `main/MainView`, `MainViewModel`); widgets in `Shared/Inc/view/` | MVVM: ViewModel subscribes to Observables, View draws through `IDisplay` | core, `IDisplay`; never a service |
| Service | F103 `Main/service/<X>Service.hpp` + `Main/service/impl/<X>ServiceImpl.{hpp,cpp}`; F051 `Main/service/` (concrete) | business logic, one responsibility each, `Input`/`Output` structs of `Observable<T>*` | other service **interfaces**, transport, driver, command, db, core |
| Command | F103 `Main/command/`, F051 `Main/command/`, `Shared/{Inc,Src}/command/` (codec, security) | frame codec, dispatcher, registry, crypto | core |
| Transport | F103 `Main/transport/{ble,wifi}/` (`Hc08Ble`, `Esp8266`, `EspFlasher`) | radio / UART-modem I/O | HAL, core |
| Driver | F103 `Main/driver/` | one class per chip (I2C sensors, ILI9341, SD card, FatFs port) | HAL, core, `Shared/Inc/view/IDisplay.hpp` |
| DB | `Shared/{Inc,Src}/db/arcanats/` | ArcanaTs time-series storage behind `IFilePort` | core |
| Core | `Shared/{Inc,Src}/core/{event,log,model,validation,types}/`, F103 `Main/core/` (+`core/model/`) | `Observable<T>`, `Model`, logging, value types, board helpers | nothing above it |
| Vendor (do not edit) | `Targets/*/Core`, `Drivers`, `Middlewares` (CubeMX), `Shared/Src/{mbedtls,nanopb}`, `uECC` | generated / third-party | — edit CubeMX code only inside `USER CODE` blocks |

Direction is one way: view → (Observables) ← service → transport/driver/command/db → core.
A lower layer never includes a higher one. Host tests live in `Tests/`, mocks in `Tests/mocks/`.

## 3. Rules

| # | Rule | Enforced by |
|---|---|---|
| R1 | An F103 service is an abstract `<X>Service` (pure virtual `initHAL/init/start/stop`, returns `ServiceStatus`) in `service/`, implemented by `<X>ServiceImpl` in `service/impl/` with a private ctor and `static <X>Service& getInstance()`. | arch-qube Interface/Impl Colocation, Naming |
| R2 | Only `AppContainer.cpp` includes `*Impl.hpp` or calls `*Impl::getInstance()`. Every other file — including other impls — holds a pointer to the interface, set in `wireServices()`. | arch-qube "Only DI Container Imports Impl" (critical); pre-commit hook |
| R3 | Services exchange data only through `Observable<T>` pointers in their `Input`/`Output` structs, wired in `AppContainer::wireServices()`; no `extern` mutable globals across tasks. Views never call services. | arch-qube Unidirectional Data Flow, View Cannot Call Service (critical) |
| R4 | Static allocation only: no `new`, `malloc`, `std::vector`/`std::string` or other heap containers. Services are function-local `static` singletons; buffers are members or `static`. FreeRTOS tasks/queues/semaphores are created during `init()`/`start()` only (`heap_4`, `configTOTAL_HEAP_SIZE` 3840 B on F103, 1536 B on F051) — never after init. | compiler flags `-fno-exceptions -fno-rtti` + nano.specs; `tools/ram-budget.sh`; review |
| R5 | `Observable<T>::publish` queues a **pointer**: publish a member/static model, never a stack local. From an ISR use `publishFromISR(&model, &woken)` then `portYIELD_FROM_ISR(woken)`; never call `publish`, `subscribe`, `LOG_*` or any blocking API in an ISR. Data shared between tasks goes through an Observable or a mutex on every access. | review (on you) |
| R6 | Every new service has a host GoogleTest in `Tests/`, registered in `Tests/CMakeLists.txt` with `add_test`. A failing test fails the image build. | `Dockerfile.test` gate; arch-qube Test Coverage; Sonar quality gate |
| R7 | New first-party sources are C++ (`.cpp`/`.hpp`). `.c` only for vendored or generated code (`*.pb.c`). | pre-commit hook |
| R8 | A new source file lives in a layer directory of §2; nothing new directly in `Main/` (except `App.cpp`, `AppContainer.*`) or in `Shared/Inc`/`Shared/Src` roots. | pre-commit hook; arch-qube Layer Direction Enforcement (critical) |
| R9 | Static RAM (`data+bss`) and flash (`text+data`) of each ELF stay within `tools/ram-budget.conf`. The F051 has 8 KB RAM and the template already uses 88% of it: shrink buffers, queue depths and task stacks before adding. Raising a budget needs the reason in the commit (and a board run on F051). | `tools/ram-budget.sh` (Jenkins "RAM/Flash Budget") |
| R10 | No credentials in source. `Main/core/Credentials.hpp` is git-ignored; CI copies `Credentials.hpp.ci`; start yours from `Credentials.hpp.template`. | `.gitignore`; Sonar secrets |
| R11 | Keep functions under cognitive complexity 15; split a large `switch` into a table or one handler per case. | Sonar quality gate |

## 4. Definition of done

Run these, in this order, and paste the last line of each into your summary. They are what
the Jenkinsfile runs:

```bash
tools/arch-check.sh                                             # arch-qube, same image + threshold as CI; must exit 0
docker build -f Dockerfile.ci -t stm32-app-build:1.0.0 .        # both firmwares (= docker compose -f docker-compose.ci.yml build); prints sizes
docker run --rm stm32-app-build:1.0.0 bash tools/ram-budget.sh build   # RAM/flash budget; must print "ram-budget: PASS"
docker build -f Dockerfile.test -t stm32-test:ci .              # host GoogleTests + coverage; must print "host tests: PASS"
```

`FAIL`, exit 2 ("NOT a pass"/"NOT measured"), or a rule shown as NOT EVALUATED for a layer
you touched means not done. There is no board in CI: a change to timing, ISRs or a driver
also needs a run on hardware, and your summary must say whether it had one.

## 5. Architecture check (arch-qube)

arch-qube (https://github.com/jrjohn/arcana-arch-qube; usage skill:
https://github.com/jrjohn/arcana-skills/tree/main/arch-qube-skill) scans the source for the
layer rules in §2–§3 with the `stm32` profile and scores them. The version is pinned in
`tools/arch-qube.version` (0.5.7). `tools/arch-qube-image.sh` tries
`ghcr.io/jrjohn/arch-qube:<version>`, then `arcana.boo/arcana/arch-qube:<version>`; if
neither is available it exits 2 — **NOT a pass**. There is no fallback to `:latest`: that
fallback silently changed arcana-embedded-esp32's CI verdict. The Jenkinsfile uses the same script.

| Situation | Run |
|---|---|
| Docker available | `tools/arch-check.sh` → report in `arch-qube-reports/arch-qube.{md,json}` |
| No Docker | `pipx run --spec git+https://github.com/jrjohn/arcana-arch-qube@v$(cat tools/arch-qube.version) arch-qube scan . --framework stm32 --no-ai --ci --format json,markdown -o arch-qube-reports --threshold 90` |
| No pipx | `python3 -m venv .aq && .aq/bin/pip install git+https://github.com/jrjohn/arcana-arch-qube@v$(cat tools/arch-qube.version)`, then `.aq/bin/arch-qube scan` with the same arguments |

PASS = score ≥ threshold (90) **and** zero CRITICAL violations. Reading the report:

- `--ci` prints nothing on failure — open `arch-qube-reports/arch-qube.md`.
- A rule under "Not evaluated" (`"evaluated": false`) was not checked. NOT EVALUATED ≠ pass:
  if it covers a layer you touched, you are not done.
- Check `files_checked` per rule; a rule that checked 0 files passed by looking at nothing.
  A 100 on code you know is flat means the scan did not see your files.

On a false positive: open an issue on arcana-arch-qube with the report JSON and the file.
NEVER lower `--threshold`, add exclusions, or move code into a vendor directory to pass.

## 6. Session checkpoints (Claude Code on John's machine)

Merged from the former `CLAUDE.md`; applies to every agent that keeps notes across sessions.

1. After every meaningful step (design decision, code written, file edited, build tested),
   update `~/.claude/projects/-Users-jrjohn-Documents-projects-arcana-embedded-stm32/memory/session_checkpoint.md`
   immediately — a CLI crash otherwise loses the session's decisions.
2. The checkpoint holds: **Task** (1 line), **Completed** (numbered steps this session),
   **Current**, **Next**, **Key Decisions**, **Updated** (ISO timestamp).
3. At session start, read that file; if the user's question matches its task, say
   "I see we were working on [X]. Want to continue?"
4. A checkpoint item that is a permanent decision (architecture, preference, design
   choice) is also saved as its own memory file.
5. When the task is fully complete (deployed, merged, or the user confirms done), clear the
   file's content but keep the file.

## 7. Reference

- `arcana-stm32-developer-skill`
  (https://github.com/jrjohn/arcana-skills/tree/main/arcana-stm32-developer-skill) —
  detailed patterns and examples. **Optional**; everything you must follow is in this file,
  and on any conflict AGENTS.md wins.
- Worked example of a service: `LedService` → `LedServiceImpl`, fed by
  `TimerService::output.BaseTimer` in `AppContainer::wireServices()`; its test is in
  `Tests/test_services_f103.cpp`.
