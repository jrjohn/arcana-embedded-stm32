# Plan: Registration-Embedded ECDH + ECDSA Key Architecture

## Context

Currently, MQTT command encryption requires a runtime ECDH key exchange via a server daemon that subscribes to MQTT topics and maintains per-device sessions. This adds complexity — the user wants to eliminate the daemon by embedding the key exchange into the existing HTTP registration flow. Re-registration automatically rotates keys.

**Goal**: One HTTP registration round-trip = TOFU identity + ECDH key agreement + ECDSA verification. No runtime handshake needed. Server becomes stateless (just DB + registration API).

## Architecture

### Registration Flow (single HTTP round-trip)

```
Device                                    Server
  │                                         │
  │ 1. Generate ephemeral EC P-256 keypair  │
  │    (dev_priv, dev_pub)                  │
  │                                         │
  │ 2. POST /api/register                   │
  │    device_id + device_key (TOFU)        │
  │    + dev_pub (64 bytes)        ────→    │
  │                                         │ 3. Verify TOFU (device_key)
  │                                         │ 4. Generate EC P-256 keypair
  │                                         │    (srv_priv, srv_pub)
  │                                         │ 5. ECDH: shared = ECDH(srv_priv, dev_pub)
  │                                         │ 6. comm_key = HKDF(shared, device_id, "ARCANA-COMM")
  │                                         │ 7. sig = ECDSA_sign(company_priv, srv_pub + device_id)
  │                                         │ 8. Store DB: device_id → srv_priv + dev_pub + comm_key
  │                                         │
  │    ←──── mqtt_creds + srv_pub + sig     │ 9. Response encrypted with device_key
  │                                         │
  │ 10. Verify ECDSA(company_pub, sig,      │
  │     srv_pub + device_id)                │
  │ 11. ECDH: shared = ECDH(dev_priv, srv_pub)
  │ 12. comm_key = HKDF(shared, device_id, "ARCANA-COMM")
  │ 13. Discard dev_priv (PFS)              │
  │ 14. Store comm_key in device.ats ch2    │
```

### After Registration (no daemon needed)

```
Device                    MQTT Broker              Any Client
  │                          │                        │
  │ sensor (comm_key) ──→    │    ←── subscribe ──    │
  │                          │    ──→ encrypted ──→   │ query DB → comm_key → decrypt
  │                          │                        │
  │    ←── command (comm_key)│    ←── publish ────    │ query DB → comm_key → encrypt
```

### Key Separation

| Key | Purpose | Location | Rotates? |
|-----|---------|----------|----------|
| fleet_master | Derive device_key | Device flash | Never |
| device_key | TOFU identity + BLE PSK + ATS encryption | Device RAM | Never (deterministic) |
| company_pub | Verify server ECDSA signatures | Device firmware | Firmware update |
| company_priv | Sign server public keys | Server HSM/config | Rarely |
| comm_key | MQTT sensor + command encryption | Device storage + Server DB | Every re-register |

### BLE (unchanged)
- PSK = device_key (independent of registration)
- ECDH P-256 session key exchange (local, no server)
- Session gate enforced (from earlier commit)

## DB Schema — device_token (Zero Secrets in DB)

### Design Principle
- **DB stores only public data** — device_pub + count + metadata
- **server_priv derived on-the-fly** from `COMPANY_PRIV` (env var) + device_id + count
- **comm_key never stored** — computed in memory per request via ECDH
- DB compromise alone cannot derive any comm_key

### Existing Tables
```sql
user          (id, username, password_hash, is_admin)       -- Mosquitto auth
acl           (id, user_id, topic, rw)                      -- Mosquitto ACL
device        (id, device_id, public_key, firmware_ver, registered_at)  -- TOFU
```

### New Table: device_token
```sql
CREATE TABLE IF NOT EXISTS device_token (
    id            INT(11) AUTO_INCREMENT PRIMARY KEY,
    client_id     VARCHAR(48)   NOT NULL,        -- device_id ("32FFD605")
    token_type    VARCHAR(40)   DEFAULT 'ecdh_p256',
    device_pub    VARCHAR(130)  NOT NULL,         -- device ECDH ephemeral public key (hex)
    scope         TEXT          DEFAULT NULL,     -- "mqtt:sensor mqtt:command"
    revoked       TINYINT(1)   DEFAULT 0,        -- 0=active, 1=rotated
    issued_at     INT(11)      NOT NULL,         -- Unix timestamp
    expires_in    INT(11)      NOT NULL,         -- seconds (31536000=1yr)
    user_id       INT(11)      DEFAULT NULL,     -- FK to user table
    firmware_ver  VARCHAR(40)   DEFAULT NULL,     -- firmware version at registration
    count         INT(11)      NOT NULL DEFAULT 1,-- registration count (key diversity)
    remote_ip     VARCHAR(45)   DEFAULT NULL,     -- device IP at registration
    updatedate    TIMESTAMP    DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

**No private keys or comm_keys in this table.** Only public key + metadata.

### Key Derivation Chain (server-side, on-the-fly)
```python
COMPANY_PRIV = os.environ["COMPANY_PRIV_HEX"]  # sole secret, in env var

def derive_comm_key(device_id, device_pub_hex, count):
    # 1. Derive per-registration server_priv from company_priv
    seed = HKDF(COMPANY_PRIV, f"{device_id}:{count}", "ARCANA-SERVER-KEY", 32)
    server_priv = ec.derive_private_key(int.from_bytes(seed), SECP256R1)

    # 2. ECDH with device's ephemeral public key
    device_pub = load_ec_pub_from_hex(device_pub_hex)
    shared = server_priv.exchange(ECDH(), device_pub)

    # 3. Derive symmetric comm_key
    comm_key = HKDF(shared, device_id, "ARCANA-COMM", 32)
    return comm_key  # in memory only, never stored
```

### Client Query Flow
```
Any Client                               Server
      │                                     │
      │  GET /api/device/<id>/key           │
      │  Authorization: Bearer <token>  ──→ │
      │                                     │ row = SELECT device_pub, count
      │                                     │   FROM device_token WHERE client_id=?
      │                                     │   AND revoked=0
      │                                     │ comm_key = derive_comm_key(
      │                                     │   id, row.device_pub, row.count)
      │  ←── { "comm_key": "ab12..." }      │  ← computed in memory, not from DB
```

### Re-registration = Key Rotation
```sql
-- 1. Revoke old (history preserved)
UPDATE device_token SET revoked=1
WHERE client_id='32FFD605' AND revoked=0;

-- 2. Insert new (count increments → different server_priv → different comm_key)
INSERT INTO device_token
  (client_id, token_type, device_pub, scope,
   issued_at, expires_in, user_id, firmware_ver, count, remote_ip)
VALUES
  ('32FFD605', 'ecdh_p256', '<new_device_pub_hex>',
   'mqtt:sensor mqtt:command', UNIX_TIMESTAMP(), 31536000,
   <user_id>, '1.0.0', <old_count+1>, '<request.remote_addr>');
```

### Security Analysis
| Compromised | Impact |
|-------------|--------|
| DB only | device_pub + count → **cannot derive comm_key** (no COMPANY_PRIV) |
| COMPANY_PRIV only | No device_pub → **cannot derive comm_key** |
| DB + COMPANY_PRIV | Can derive all comm_keys → **full compromise** (server is fully owned) |
| Single comm_key leak | Only one device affected, re-register to rotate |

## Implementation Steps

### Step 0: DB Migration — Add device_token table
**File**: `tools/server/registration_api.py` (`init_db()`)

Add `CREATE TABLE IF NOT EXISTS device_token` in `init_db()`. Also add the key query endpoint.

### Step 1: Protobuf Update
**File**: `Shared/proto/registration.proto`

```protobuf
message RegisterRequest {
    string device_id = 1;
    bytes  device_key = 2;      // renamed from public_key (TOFU identity)
    uint32 firmware_ver = 3;
    bytes  ecdh_pub = 4;        // NEW: device ephemeral EC P-256 public key (64B)
}

message RegisterResponse {
    bool   success = 1;
    string mqtt_user = 2;
    string mqtt_pass = 3;
    string mqtt_broker = 4;
    uint32 mqtt_port = 5;
    string upload_token = 6;
    string topic_prefix = 7;
    string error = 8;
    bytes  server_pub = 9;      // NEW: server EC P-256 public key (64B)
    bytes  ecdsa_sig = 10;      // NEW: ECDSA signature over (server_pub + device_id)
}
```

### Step 2: Device — Embed company_public_key
**File**: `Targets/stm32f103ze/Services/Common/CompanyKey.hpp` (NEW)

- Header-only, stores company ECDSA public key as `constexpr uint8_t[64]`
- Used by RegistrationServiceImpl to verify server_pub signature
- Updated via firmware OTA if company key rotates

### Step 3: Device — Registration with ECDH
**File**: `Targets/stm32f103ze/Services/Service/impl/RegistrationServiceImpl.cpp`

Changes:
1. In `httpRegister()`: generate ephemeral EC P-256 keypair (mbedtls)
2. Add `ecdh_pub` to RegisterRequest protobuf
3. Parse `server_pub` + `ecdsa_sig` from RegisterResponse
4. Verify ECDSA signature: `mbedtls_ecdsa_verify(company_pub, hash(server_pub + device_id))`
5. Compute ECDH: `shared = mbedtls_ecdh_compute_shared(dev_priv, server_pub)`
6. Derive: `comm_key = HKDF-SHA256(shared, device_id, "ARCANA-COMM")`
7. Discard dev_priv (PFS)
8. Store comm_key alongside mqtt credentials

**Stack note**: ECDH + ECDSA needs ~3KB stack. Registration runs in MQTT task (4KB stack) — sufficient.

### Step 4: Device — Use comm_key for MQTT
**Files**:
- `MqttServiceImpl.cpp`: sensor publish uses comm_key instead of device_key for ChaCha20
- `CommandBridge.cpp`: PSK init uses comm_key instead of device_key for AES-256-CCM
- `RegistrationServiceImpl.cpp`: expose comm_key getter

Changes:
- Add `getCommKey()` to RegistrationServiceImpl (returns stored comm_key)
- MqttServiceImpl::publishSensorData: `ChaCha20::crypt(regSvc.getCommKey(), ...)`
- CommandBridge constructor: use comm_key from RegistrationService instead of DeviceKey::deriveKey()
- Fallback: if no comm_key (not registered yet), use device_key

### Step 5: Device — Store comm_key
**File**: `RegistrationServiceImpl.cpp` credential storage

- Add comm_key[32] to Credentials struct
- Saved to device.ats ch2 and creds.enc alongside mqtt credentials
- Loaded on boot before MQTT connect

### Step 6: Server — Derive keypair + sign + store device_token
**File**: `tools/server/registration_api.py`

Changes to `handle_register()`:
1. Parse `ecdh_pub` from RegisterRequest (field 4)
2. Get current count: `SELECT COALESCE(MAX(count),0)+1 FROM device_token WHERE client_id=?`
3. **Derive server_priv** from company_priv + device_id + count:
   ```python
   seed = HKDF(COMPANY_PRIV, f"{device_id}:{count}", "ARCANA-SERVER-KEY", 32)
   server_priv = ec.derive_private_key(int.from_bytes(seed), SECP256R1)
   server_pub = server_priv.public_key()  # raw x||y 64 bytes
   ```
4. Compute ECDH: `shared = server_priv.exchange(ECDH(), device_ecdh_pub)`
5. Derive comm_key: `HKDF(shared, device_id, "ARCANA-COMM")`
   — comm_key only used to verify derivation works, NOT stored in DB
6. Sign: `sig = ecdsa_sign(company_priv, server_pub_bytes + device_id.encode())`
7. **Revoke old token**: `UPDATE device_token SET revoked=1 WHERE client_id=? AND revoked=0`
8. **Insert new token** (no secrets — only device_pub + count):
   ```sql
   INSERT INTO device_token
     (client_id, device_pub, issued_at, expires_in, user_id,
      firmware_ver, count, remote_ip)
   VALUES (?, ?, UNIX_TIMESTAMP(), 31536000, ?, ?, ?, ?)
   ```
9. Add `server_pub` + `ecdsa_sig` to RegisterResponse

Changes to `init_db()`:
- Add `CREATE TABLE IF NOT EXISTS device_token (...)` from DB Schema section

### Step 7: Server — Client key query API
**File**: `tools/server/registration_api.py` (new endpoint)

```python
@app.route("/api/device/<device_id>/key", methods=["GET"])
def get_device_key(device_id):
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        abort(401)
    # TODO: admin token verification

    conn = get_db()
    with conn.cursor() as cur:
        cur.execute("""
            SELECT device_pub, count, issued_at, expires_in
            FROM device_token
            WHERE client_id=%s AND revoked=0
            ORDER BY issued_at DESC LIMIT 1
        """, (device_id,))
        row = cur.fetchone()
    conn.close()

    if not row:
        abort(404, "No active token for device")

    # Derive comm_key on-the-fly (never stored)
    comm_key = derive_comm_key(device_id, row["device_pub"], row["count"])

    return jsonify({
        "device_id": device_id,
        "comm_key": comm_key.hex(),
        "issued_at": row["issued_at"],
        "expires_in": row["expires_in"]
    })
```

Any authorized client queries this endpoint. comm_key is computed in memory from `COMPANY_PRIV` (env var) + DB public data, never persisted.

### Step 8: Update Python test tools
**File**: `tools/mqtt_crypto_test.py`

- Query comm_key from server API instead of using PSK_HEX env var
- Remove ECDH key exchange flow (no longer needed at runtime)
- Simplify to: get comm_key → encrypt/decrypt directly

### Step 9: Remove MQTT runtime KE requirement
**File**: `CommandBridge.cpp`

- MQTT commands no longer need runtime ECDH session
- PSK (comm_key) is sufficient since it rotates on re-registration
- Keep BLE session gate (BLE still uses device_key + ECDH)
- Remove MQTT-specific KE handling in MqttServiceImpl (lines 582-652)

## Files Modified

| File | Change |
|------|--------|
| `Shared/proto/registration.proto` | Add ecdh_pub, server_pub, ecdsa_sig fields |
| `Services/Common/CompanyKey.hpp` | NEW — company ECDSA public key |
| `Services/Service/impl/RegistrationServiceImpl.cpp` | ECDH + ECDSA verify + comm_key storage |
| `Services/Service/impl/MqttServiceImpl.cpp` | Use comm_key for sensor encryption |
| `Services/Service/impl/CommandBridge.cpp` | Use comm_key as PSK, remove MQTT KE |
| `Services/Model/F103Models.hpp` | Add comm_key to Credentials struct |
| `tools/server/registration_api.py` | Generate keypair + ECDSA sign + store DB |
| `tools/mqtt_crypto_test.py` | Query comm_key from API |

## Verification

1. **Registration test**: `python3 tools/server/test_register.py` — verify server_pub + signature in response
2. **ECDSA verify**: device serial output should show `[I][REG] 0x0D10` (REG_OK) with signature verified
3. **Sensor decrypt**: `python3 tools/mqtt_monitor.py` — query comm_key from API, decrypt sensor messages
4. **Command test**: `python3 tools/mqtt_crypto_test.py --cmd ping` — uses comm_key from API
5. **Re-registration**: force re-register, verify new comm_key in DB, old one invalid
6. **BLE**: verify BLE still works with device_key ECDH (independent of registration)
7. **Build**: zero errors, RAM/flash within budget

## Complexity Reduction

| Before | After |
|--------|-------|
| Server daemon subscribes MQTT, maintains sessions | No daemon needed |
| Runtime ECDH over MQTT (per device, per session) | Key exchange in registration (one-time HTTP) |
| PSK + optional session key | Single comm_key (rotates on re-register) |
| Client needs to do KE handshake | Client queries DB for comm_key |
| 2 key types for MQTT (PSK + session) | 1 key type (comm_key) |
