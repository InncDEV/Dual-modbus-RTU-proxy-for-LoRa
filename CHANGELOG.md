# Changelog

All notable changes to this project are documented in this file.

## [v3.0] — 2026-03-03

Architectural rewrite: transparent buffer replaced with a dual Modbus proxy.

### Added
- **Virtual Slave** — responds to the real master from cached data within ~1 ms.
- **Virtual Master** — polls the real slave over LoRa round-robin with configurable timeout.
- **Auto-learning** — register map is built automatically from master requests (FC03/FC04).
- **Cache invalidation** via consecutive failure counter (`CACHE_MAX_FAILURES`).
- **Non-blocking forwarding** — cache-to-master writes never stall the main loop.
- **Janitza UMG 104 register decode** — IEEE 754 float values printed with named registers in debug output.
- **Periodic statistics** — hit/miss rates, LoRa success/failure, per-entry cache age printed every 30 s.
- GPL-3.0 license.

### Changed
- Serial port assignment clarified (Serial1 = LoRa, Serial2 = master).
- Configuration moved to `include/config.h` with scenario-specific rationale comments.

### Removed
- `CACHE_MAX_AGE_MS` — freshness is now driven entirely by failure counting, not wall-clock age.

## [v2.0] — 2026-02-xx

Transparent Modbus RTU frame buffer with auto-baud detection.

### Added
- Auto-baud detection across standard Modbus rates.
- CRC-16 validation with configurable check modes (off / log / filter).
- T3.5 frame detection with auto-calculated timing.
- Status LED toggling on forwarded frames.
- Debug hex dump of forwarded frames.

### Changed
- Replaced fixed-delay forwarding with proper T3.5 silence detection.

## [v1.0] — 2026-01-xx

Initial transparent byte-forwarding buffer.

### Added
- Simple bidirectional byte relay between master serial and LoRa serial.
- Fixed baud rate configuration.
- Basic debug output.

### Known Issues
- No frame awareness — bytes forwarded individually, causing LoRa fragmentation.
- Master timeout failures due to LoRa latency (0.5–4 s round-trip).
