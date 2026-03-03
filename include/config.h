// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 InncDEV — https://github.com/InncDEV

#pragma once

// ============================================================
//  MODBUS LORA DUAL PROXY - Configuration
// ============================================================
//  Hardware: Arduino Mega 2560 Pro
//  Purpose:  Virtual Slave serves master from cache while
//            Virtual Master polls the real slave through LoRa
//
//  Scenario: 1 energy meter, FC03 (Read Holding Registers),
//            3-5 register groups, USR-LG206 at SF8/SF9.
//            Expected LoRa round-trip: ~0.5-2 s per request.
//            Full cache refresh cycle:  ~3-10 s (3-5 groups).
// ============================================================

// --- Serial Port Assignment --------------------------------
//  D16(TX) D17(RX) → RS485 XY-017 → Modbus Master  (Arduino Serial2)
//  D18(TX) D19(RX) → RS485 XY-017 → USR-LG206 LoRa (Arduino Serial1)
//  USB              → Debug monitor                  (Arduino Serial)
//
//  NOTE: Arduino's Serial1/Serial2 numbering is opposite to the
//        board's silk-screen labels on some Mega 2560 Pro boards.
// -----------------------------------------------------------
#define MASTER_SERIAL  Serial2   // pins D16, D17
#define LORA_SERIAL    Serial1   // pins D18, D19
#define DEBUG_SERIAL   Serial

// --- Baud Rate ---------------------------------------------
#define MODBUS_BAUD          9600     // fixed baud rate (0 = auto-detect)
#define MODBUS_BAUD_DEFAULT  9600     // fallback if auto-detect fails
#define LORA_BAUD            0        // 0 = same as Modbus baud
#define MODBUS_CONFIG        SERIAL_8N1

// --- Frame Detection Timing --------------------------------
//  Master side: 0 = auto-calculate 3.5 char times from baud rate.
//  LoRa side:   inter-byte gap timeout for detecting corrupt/incomplete
//               data.  Normal frames are detected by expected-length + CRC.
#define MASTER_FRAME_TIMEOUT_US  0       // 0 = auto (T3.5)
#define LORA_FRAME_TIMEOUT_US    50000   // 50 ms gap → discard corrupt data

// --- Cache / Proxy -----------------------------------------
//  Tuned for: 1 slave, 3-5 register groups, SF8/SF9 LoRa.
//
//  LORA_RESPONSE_TIMEOUT_MS  SF8/SF9 round-trip is typically 200-600ms.
//                            1 s gives margin while keeping failure
//                            detection fast.  Increase if you see false
//                            timeouts in the debug log.
//  CACHE_MAX_FAILURES        2 consecutive LoRa failures for a given
//                            entry → cache invalidated → master sees
//                            timeout.  With 5 groups and 1s per poll,
//                            worst-case detection time ≈ 2 × 5 × 1 = 10 s.
//                            Normal data freshness = polling cycle time
//                            (~2s/group × N groups).
//  VM_INTER_REQUEST_MS       Only 1 slave on the LoRa link, no bus
//                            contention.  50 ms is enough for the LG206
//                            to switch RX→TX.
#define MAX_CACHE_ENTRIES        10
#define LORA_RESPONSE_TIMEOUT_MS 1000
#define CACHE_MAX_FAILURES       2
#define VM_INTER_REQUEST_MS      50

// --- Buffer ------------------------------------------------
#define MAX_FRAME_SIZE  256  // Modbus RTU max ADU size

// --- CRC Validation ----------------------------------------
//  0 = disabled  (no CRC check on master requests — risky)
//  1 = log only  (check CRC, log errors, still process)
//  2 = filter    (drop master requests with bad CRC — recommended)
#define CRC_CHECK_MODE  2

// --- Debug Output ------------------------------------------
#define DEBUG_ENABLED       1
#define DEBUG_BAUD          115200
#define STATS_INTERVAL_MS   30000   // periodic stats every 30 s

// --- Status LED --------------------------------------------
#define STATUS_LED  LED_BUILTIN     // pin 13 on Mega 2560 Pro
