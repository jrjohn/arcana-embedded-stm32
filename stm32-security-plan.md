# Secure Key Provisioning — Source Code Public, Data Still Undecryptable

## Context

**Problem**: `DeviceKey.hpp` 硬編碼 32-byte master secret。任何人有 GitHub 原始碼 + .ats 檔案中的 UID → 推導出 device key → 解密所有資料。加密形同虛設。

**Goal**: 即使攻擊者擁有完整原始碼 + 加密演算法知識 + 裝置 UID，仍無法解密 .ats 資料（Kerckhoffs 原則）。

**Solution**: 將 master secret 從原始碼移到 **製造時寫入 Flash 的 per-device 隨機金鑰**，搭配 RDP Level 1 鎖定 debug port。

---

## Flash Memory Layout

```
0x08000000 ┌──────────────────┐
           │   Bootloader     │ 32KB
0x08008000 ├──────────────────┤
           │                  │
           │   App Code       │ 476KB
           │                  │
0x0807F000 ├──────────────────┤
           │   Key Page A     │ 2KB (primary)
0x0807F800 ├──────────────────┤
           │   Key Page B     │ 2KB (backup)
0x08080000 └──────────────────┘
```

**Key Page Format** (64 bytes):

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 4 | Magic `"KEY1"` (0x4B455931) |
| 0x04 | 1 | Version: 1 |
| 0x05 | 1 | Key length: 32 |
| 0x06 | 2 | Flags (bit0: provisioned) |
| 0x08 | 32 | **Per-device secret** (random, never in source) |
| 0x28 | 4 | Provisioning timestamp |
| 0x2C | 4 | Serial number |
| 0x30 | 4 | CRC-32 of bytes 0x00-0x2F |
| 0x34 | 12 | Reserved (0xFF) |

---

## Key Derivation (Algorithm Unchanged)

```
Before: device_key = ChaCha20(HARDCODED_MASTER, uid, 0)  ← master in source code
After:  device_key = ChaCha20(FLASH_SECRET,     uid, 0)  ← secret in protected flash
```

Same ChaCha20 engine, same derivation algorithm. Only the secret input source changes.

---

## Implementation Plan

### Phase 1: Key Store Infrastructure

**1. New file: `Services/Common/KeyStore.hpp`** (header-only)
- Read key page from flash address `0x0807F000`
- Validate magic + CRC-32
- Fallback to Page B if Page A invalid
- Return 32-byte secret or "unprovisioned" status

**2. Modify: `Services/Common/DeviceKey.hpp`**
- Replace hardcoded `static const uint8_t master[32]` with `KeyStore::readSecret()`
- If unprovisioned: fallback to legacy key (backward compat for dev boards)

**3. Modify: `STM32F103ZETX_FLASH.ld`**
- Reduce App FLASH from 480KB to 476KB
- Add `KEYSTORE` region: `KEYSTORE (r) : ORIGIN = 0x0807F000, LENGTH = 4K`

**4. Modify: bootloader `bl_flash.c`**
- Skip key pages during OTA erase: `if (addr >= 0x0807F000) continue;`

**5. Modify: `Shared/Inc/ota_header.h`**
- Add `KEY_STORE_BASE = 0x0807F000` shared constant

### Phase 2: Provisioning Tooling

**6. New file: `tools/provision_key.py`**
```python
# 1. Generate random 32-byte secret
# 2. Read device UID via OpenOCD
# 3. Derive device_key for verification
# 4. Write key pages to flash via OpenOCD
# 5. Store (uid, secret, device_key) in provisioning DB
# 6. Optionally enable RDP Level 1
```

**7. Modify: `tools/arcanats.py`**
- Add `--secret SECRET_HEX` mode
- Auto-read UID from .ats file header
- Derive key: `device_key = ChaCha20(secret, uid, 0)`

### Phase 3: Production Hardening

**8. RDP Level 1** — provisioning script enables after key write
- Debug port connected → flash mass-erased (key destroyed)
- CPU can still read own flash normally

**9. LCD warning** — unprovisioned device shows toast "NOT PROVISIONED"

---

## Security Analysis

| Attack | Before | After |
|--------|--------|-------|
| Has source code + UID | Derive key → decrypt | Secret not in source → blocked |
| Dumps flash via SWD | Read master + UID → decrypt | RDP Level 1 → mass erase triggered |
| Has .ats file only | UID in header, master in source → decrypt | UID in header, secret in flash → blocked |
| Steals SD card | Decrypt with source code | Cannot derive key without provisioned secret |
| Compromises 1 device | Key = same master for all → fleet compromise | Per-device secret → only that device |

---

## Migration (Backward Compatibility)

1. **Unprovisioned device** (key pages = 0xFF): fallback to legacy hardcoded master
2. **Provisioned device** (key pages = valid "KEY1"): use flash secret
3. Old .ats files remain readable with legacy key via `arcanats.py --key OLD_KEY`
4. New .ats files use provisioned key via `arcanats.py --secret SECRET_HEX`

---

## Files to Modify

| File | Change |
|------|--------|
| `Services/Common/KeyStore.hpp` | **NEW** — read/validate key pages from flash |
| `Services/Common/DeviceKey.hpp` | Replace hardcoded master with KeyStore read |
| `STM32F103ZETX_FLASH.ld` | Reduce App flash, add KEYSTORE region |
| `Shared/Inc/ota_header.h` | Add KEY_STORE_BASE constant |
| `bl_flash.c` (bootloader) | Skip key pages during OTA erase |
| `tools/provision_key.py` | **NEW** — manufacturing provisioning script |
| `tools/arcanats.py` | Add --secret mode |
| `Services/View/MainView.cpp` | Toast warning if unprovisioned |

---

## Verification

1. **Build**: zero warnings, binary < 476KB (leaves room for key pages)
2. **Unprovisioned boot**: device uses legacy key, LCD shows "NOT PROVISIONED" toast
3. **Provision**: run `provision_key.py`, verify key pages written
4. **Provisioned boot**: device uses flash secret, no warning
5. **arcanats.py --secret**: correctly derives key and decrypts new .ats files
6. **RDP test**: connect SWD after RDP Level 1 → flash mass-erased, key destroyed
7. **OTA test**: firmware update preserves key pages (bootloader skips them)
8. **Power loss**: key pages survive (no erase during normal operation)

---

## RAM/Flash Cost

| Resource | Cost |
|----------|------|
| Flash | -4KB app space (476KB → still 4x more than needed) |
| RAM | 0 (key read to existing stack variable) |
| Code | ~200 bytes (KeyStore read + CRC validate) |
