# Modbus LoRa Dual Proxy

Dual Modbus RTU proxy for Arduino Mega 2560 Pro.  
Bridges a fast local Modbus master (~500 ms timeout) to a remote slave over a USR-LG206 LoRa link (1-4 s round-trip) by splitting the connection into two independent Modbus participants connected by a shared data cache.

## Problem

LoRa radio links add 0.5-4 seconds of latency per request/response cycle.  
A Modbus master with a 500 ms response timeout will always time out waiting for the slave — the link is simply too slow for transparent forwarding.

## Solution

```
[Real Master]                                            [Real Slave]
 (energy mgmt)                                           (energy meter)
      │ RS-485                                      RS-485 via LoRa
      │ ~500 ms timeout                             ~0.5-2 s round-trip
 ┌────┴────────────── Mega 2560 Pro ─────────────────┴────┐
 │  VIRTUAL SLAVE         SHARED CACHE     VIRTUAL MASTER  │
 │  (Serial2)            ┌──────────┐      (Serial1/LoRa)  │
 │                       │ entry 0  │                       │
 │  Receives requests ──►│ entry 1  │◄── Polls slave        │
 │  Replies from cache   │ ...      │    Stores responses   │
 │  within ~1 ms         │ valid?   │    Round-robin         │
 │                       └──────────┘                       │
 └──────────────────────────────────────────────────────────┘
```

### Virtual Slave (master-facing, Serial2)

- Listens for Modbus RTU requests from the real master.
- **Auto-learns** the register map: every unique (address, function, start register, quantity) the master requests is recorded.
- Serves cached responses instantly — well within the master's timeout.
- If no valid cache exists (startup or slave offline): stays silent so the master sees its normal timeout.

### Virtual Master (LoRa-facing, Serial1)

- Independently cycles through all learned register groups **round-robin** via the LoRa link.
- Uses a longer timeout (configurable, default 1 s) to accommodate LoRa latency.
- On success: stores the raw slave response in the cache.
- On timeout or exception: increments a failure counter for that entry.

### Cache Validity

An entry is valid when:

1. At least one successful response has been received.
2. Fewer than `CACHE_MAX_FAILURES` (default 2) consecutive LoRa failures.

If the slave goes offline, failures accumulate and the cache is invalidated — the master discovers the timeout naturally.

## Hardware

| Component | Connection |
|---|---|
| Mega 2560 Pro D16 (TX) D17 (RX) | RS-485 transceiver XY-017 → Modbus master bus |
| Mega 2560 Pro D18 (TX) D19 (RX) | RS-485 transceiver XY-017 → USR-LG206 RS-485 |
| Mega 2560 Pro USB | Debug monitor (115200 baud) |

The XY-017 / HW-0519 modules have **auto direction control** — no DE/RE pin wiring needed.

## Configuration

All parameters are in `include/config.h`:

| Parameter | Default | Description |
|---|---|---|
| `MODBUS_BAUD` | `9600` | Serial baud rate for master and LoRa ports |
| `LORA_RESPONSE_TIMEOUT_MS` | `1000` | Max wait for slave response via LoRa |
| `CACHE_MAX_FAILURES` | `2` | Consecutive LoRa failures before cache invalidation |
| `MAX_CACHE_ENTRIES` | `10` | Max unique register groups the proxy can learn |
| `VM_INTER_REQUEST_MS` | `50` | Pause between consecutive LoRa polls |
| `CRC_CHECK_MODE` | `2` | 0 = off, 1 = log errors, 2 = drop bad-CRC frames |

## Register Decode (Janitza UMG 104)

The debug output decodes Janitza UMG 104 IEEE 754 float values with named registers for the 19000 range (voltages, currents, power, energy). Unknown registers are displayed as raw address = value.

```
970ms L>VM [OK]     Addr=1 FC=0x03 Reg=19000 Qty=34 Resp=73B
  UL1N=240.6  UL2N=240.6  UL3N=240.8  UL12=416.4  UL23=416.9  UL31=417.2
  IL1=0.0  IL2=0.0  IL3=0.0  Isum=0.0  PL1=0.0  PL2=-0.0  PL3=0.0  Psum=0.0
  SL1=0.0  SL2=0.0  SL3=0.0
```

## Master Response Timeout

The master no longer needs an extended timeout — the Virtual Slave responds from cache in under 1 ms. Keep the master's default timeout (typically 500 ms-1 s).

## Build & Upload (PlatformIO)

```bash
pio run -t upload
pio device monitor        # opens debug serial at 115200
```

## Build & Upload (Arduino IDE)

1. Copy `src/main.cpp` → `modbus_buffer/modbus_buffer.ino`
2. Copy `include/config.h` and `include/janitza_regs.h` → `modbus_buffer/`
3. Select board **Arduino Mega or Mega 2560**.
4. Upload.

## Debug Output

Connect to USB serial at 115200 baud:

```
╔═══════════════════════════════════════════╗
║  Modbus LoRa Dual Proxy  v3.0           ║
║  Virtual Slave  → cache → Master        ║
║  Virtual Master → LoRa  → Real Slave    ║
╚═══════════════════════════════════════════╝
Modbus baud  : 9600
LoRa baud    : 9600
Master T3.5  : 4164 us
LoRa timeout : 1000 ms
Max failures : 2
Waiting for master requests to learn register map...
```

Statistics print every 30 seconds showing cache hit/miss rates, LoRa success/failure counts, and per-entry age.

## Resource Usage (v3.0)

| Resource | Used | Available | Utilization |
|---|---|---|---|
| RAM | 4,462 B | 8,192 B | 54.5% |
| Flash | 10,462 B | 253,952 B | 4.1% |

## License

MIT
