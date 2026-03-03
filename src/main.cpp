// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 InncDEV — https://github.com/InncDEV

#include <Arduino.h>
#include "config.h"
#include "janitza_regs.h"

// ── Cache entry ─────────────────────────────────────────────
//  Each unique (addr, func, startReg, qty) request from the
//  real master gets one slot.  The virtual master polls all
//  learned slots round-robin through LoRa, storing responses.
//  The virtual slave replays cached responses to the master.

struct CacheEntry {
    uint8_t  request[8];                // original master frame (FC03/FC04)
    uint8_t  response[MAX_FRAME_SIZE];  // raw slave response (with CRC)
    uint16_t responseLen;
    uint32_t updatedMs;                 // millis() of last good update
    uint8_t  failCount;                 // consecutive LoRa failures
    bool     hasData;                   // true after first successful response
};

static CacheEntry cache[MAX_CACHE_ENTRIES];
static uint8_t    cacheCount = 0;

// ── Virtual Master state ────────────────────────────────────
enum VMState : uint8_t { VM_IDLE, VM_WAITING };

static VMState   vmState     = VM_IDLE;
static uint8_t   vmPollIdx   = 0;      // round-robin counter
static uint8_t   vmActiveIdx = 0;      // cache index currently being polled
static uint32_t  vmSentMs    = 0;      // when request was sent to LoRa
static uint32_t  vmDoneMs    = 0;      // when last poll completed/timed out

// ── Non-blocking forwarding → master ────────────────────────
static uint8_t  fwdBuf[MAX_FRAME_SIZE];
static uint16_t fwdLen = 0;
static uint16_t fwdIdx = 0;

// ── Receive buffers ─────────────────────────────────────────
static uint8_t  masterBuf[MAX_FRAME_SIZE];
static uint16_t masterLen        = 0;
static uint32_t masterLastByteUs = 0;

static uint8_t  loraBuf[MAX_FRAME_SIZE];
static uint16_t loraLen        = 0;
static uint32_t loraLastByteUs = 0;

// ── Runtime ─────────────────────────────────────────────────
static long     activeBaud      = MODBUS_BAUD_DEFAULT;
static uint32_t masterTimeoutUs = 0;

// ── Counters ────────────────────────────────────────────────
static uint32_t statsMasterReq   = 0;
static uint32_t statsCacheServed = 0;
static uint32_t statsCacheMiss   = 0;
static uint32_t statsLoraReq     = 0;
static uint32_t statsLoraOk      = 0;
static uint32_t statsLoraTimeout = 0;
static uint32_t statsLoraCrcErr  = 0;
static uint32_t statsLoraExcept  = 0;
static uint32_t statsMasterBad   = 0;
static uint32_t lastFrameMs      = 0;

// ============================================================
//  CRC-16 / Modbus
// ============================================================
static uint16_t crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static bool crcValid(const uint8_t *frame, uint16_t len) {
    if (len < 4) return false;
    return crc16(frame, len) == 0;
}

static uint32_t calcT35(long baud) {
    if (baud > 19200) return 1750;
    return (10UL * 1000000UL / baud) * 4;
}

// ============================================================
//  Cache helpers
// ============================================================
static CacheEntry* cacheFind(const uint8_t *req) {
    for (uint8_t i = 0; i < cacheCount; i++)
        if (memcmp(cache[i].request, req, 6) == 0)
            return &cache[i];
    return nullptr;
}

static CacheEntry* cacheAdd(const uint8_t *req) {
    if (cacheCount >= MAX_CACHE_ENTRIES) return nullptr;
    CacheEntry *e = &cache[cacheCount++];
    memcpy(e->request, req, 8);
    e->responseLen = 0;
    e->updatedMs   = 0;
    e->failCount   = 0;
    e->hasData     = false;
    return e;
}

static bool cacheIsValid(const CacheEntry *e) {
    if (!e || !e->hasData) return false;
    if (e->failCount >= CACHE_MAX_FAILURES) return false;
    return true;
}

// ============================================================
//  Debug helpers
// ============================================================
#if DEBUG_ENABLED

static void printHex(uint8_t b) {
    if (b < 0x10) DEBUG_SERIAL.print('0');
    DEBUG_SERIAL.print(b, HEX);
}

static void printReqInfo(const uint8_t *d) {
    DEBUG_SERIAL.print(F("Addr="));
    DEBUG_SERIAL.print(d[0]);
    DEBUG_SERIAL.print(F(" FC=0x"));
    printHex(d[1]);
    uint16_t reg = ((uint16_t)d[2] << 8) | d[3];
    uint16_t qty = ((uint16_t)d[4] << 8) | d[5];
    DEBUG_SERIAL.print(F(" Reg="));
    DEBUG_SERIAL.print(reg);
    DEBUG_SERIAL.print(F(" Qty="));
    DEBUG_SERIAL.print(qty);
}

static void printHexDump(const uint8_t *data, uint16_t len) {
    uint16_t show = min(len, (uint16_t)20);
    for (uint16_t i = 0; i < show; i++) {
        printHex(data[i]);
        DEBUG_SERIAL.print(' ');
    }
    if (len > show) DEBUG_SERIAL.print(F("..."));
}

static void logTs(const __FlashStringHelper *tag) {
    DEBUG_SERIAL.println();
    DEBUG_SERIAL.print(millis());
    DEBUG_SERIAL.print(F("ms "));
    DEBUG_SERIAL.print(tag);
}

static float decodeFloat(const uint8_t *b) {
    // Janitza big-endian IEEE 754: bytes are [B3 B2 B1 B0]
    union { float f; uint32_t u; } v;
    v.u = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
          ((uint32_t)b[2] << 8)  | b[3];
    return v.f;
}

static const char* regLookup(uint16_t addr) {
    for (uint8_t i = 0; i < JANITZA_REG_COUNT; i++) {
        uint16_t a = pgm_read_word(&JANITZA_REGS[i].addr);
        if (a == addr)
            return (const char*)pgm_read_ptr(&JANITZA_REGS[i].name);
    }
    return nullptr;
}

static void printDecodedResponse(const uint8_t *req, const uint8_t *resp,
                                 uint16_t respLen) {
    uint16_t startReg = ((uint16_t)req[2] << 8) | req[3];
    uint8_t  byteCount = resp[2];
    const uint8_t *data = resp + 3;
    uint8_t  numFloats = byteCount / 4;

    DEBUG_SERIAL.print(F("  "));
    for (uint8_t i = 0; i < numFloats; i++) {
        uint16_t regAddr = startReg + i * 2;
        const char *name = regLookup(regAddr);
        float val = decodeFloat(data + i * 4);

        if (name) {
            char buf[8];
            strncpy_P(buf, name, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            DEBUG_SERIAL.print(buf);
        } else {
            DEBUG_SERIAL.print(regAddr);
        }
        DEBUG_SERIAL.print('=');
        DEBUG_SERIAL.print(val, 1);

        if (i < numFloats - 1) DEBUG_SERIAL.print(F("  "));
        if ((i & 0x07) == 7 && i < numFloats - 1) {
            DEBUG_SERIAL.println();
            DEBUG_SERIAL.print(F("  "));
        }
    }
    DEBUG_SERIAL.println();
}

static void logStats() {
    DEBUG_SERIAL.println();
    DEBUG_SERIAL.println(F("╔═════════════════════ Stats ═════════════════════╗"));
    DEBUG_SERIAL.print(F("║ Master requests            : ")); DEBUG_SERIAL.println(statsMasterReq);
    DEBUG_SERIAL.print(F("║ Cache served               : ")); DEBUG_SERIAL.println(statsCacheServed);
    DEBUG_SERIAL.print(F("║ Cache miss (master timeout) : ")); DEBUG_SERIAL.println(statsCacheMiss);
    DEBUG_SERIAL.print(F("║ LoRa polls sent            : ")); DEBUG_SERIAL.println(statsLoraReq);
    DEBUG_SERIAL.print(F("║ LoRa responses OK          : ")); DEBUG_SERIAL.println(statsLoraOk);
    DEBUG_SERIAL.print(F("║ LoRa timeouts              : ")); DEBUG_SERIAL.println(statsLoraTimeout);
    DEBUG_SERIAL.print(F("║ LoRa CRC / corrupt         : ")); DEBUG_SERIAL.println(statsLoraCrcErr);
    DEBUG_SERIAL.print(F("║ LoRa slave exceptions      : ")); DEBUG_SERIAL.println(statsLoraExcept);
    DEBUG_SERIAL.print(F("║ Master CRC errors          : ")); DEBUG_SERIAL.println(statsMasterBad);
    DEBUG_SERIAL.print(F("║ Cache slots                : "));
        DEBUG_SERIAL.print(cacheCount); DEBUG_SERIAL.print('/');
        DEBUG_SERIAL.println(MAX_CACHE_ENTRIES);
    for (uint8_t i = 0; i < cacheCount; i++) {
        DEBUG_SERIAL.print(F("║  #")); DEBUG_SERIAL.print(i);
        DEBUG_SERIAL.print(F(" Addr=")); DEBUG_SERIAL.print(cache[i].request[0]);
        DEBUG_SERIAL.print(F(" Reg="));
        uint16_t reg = ((uint16_t)cache[i].request[2] << 8) | cache[i].request[3];
        DEBUG_SERIAL.print(reg);
        DEBUG_SERIAL.print(F(" Qty="));
        uint16_t qty = ((uint16_t)cache[i].request[4] << 8) | cache[i].request[5];
        DEBUG_SERIAL.print(qty);
        if (cache[i].hasData) {
            DEBUG_SERIAL.print(F("  OK age="));
            DEBUG_SERIAL.print((millis() - cache[i].updatedMs) / 1000);
            DEBUG_SERIAL.print('s');
        } else {
            DEBUG_SERIAL.print(F("  --"));
        }
        if (cache[i].failCount > 0) {
            DEBUG_SERIAL.print(F(" fail=")); DEBUG_SERIAL.print(cache[i].failCount);
        }
        DEBUG_SERIAL.println();
    }
    DEBUG_SERIAL.println(F("╚════════════════════════════════════════════════╝"));
}

#endif

// ============================================================
//  Setup
// ============================================================
void setup() {
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH);

    DEBUG_SERIAL.begin(DEBUG_BAUD);
    delay(100);

#if DEBUG_ENABLED
    DEBUG_SERIAL.println();
    DEBUG_SERIAL.println(F("╔═══════════════════════════════════════════╗"));
    DEBUG_SERIAL.println(F("║  Modbus LoRa Dual Proxy  v3.0           ║"));
    DEBUG_SERIAL.println(F("║  Virtual Slave  → cache → Master        ║"));
    DEBUG_SERIAL.println(F("║  Virtual Master → LoRa  → Real Slave    ║"));
    DEBUG_SERIAL.println(F("╚═══════════════════════════════════════════╝"));
#endif

    activeBaud = (MODBUS_BAUD > 0) ? MODBUS_BAUD : MODBUS_BAUD_DEFAULT;
    masterTimeoutUs = (MASTER_FRAME_TIMEOUT_US > 0)
                        ? (uint32_t)MASTER_FRAME_TIMEOUT_US
                        : calcT35(activeBaud);

    long loraBaud = (LORA_BAUD > 0) ? LORA_BAUD : activeBaud;
    MASTER_SERIAL.begin(activeBaud, MODBUS_CONFIG);
    LORA_SERIAL.begin(loraBaud, MODBUS_CONFIG);

#if DEBUG_ENABLED
    DEBUG_SERIAL.print(F("Modbus baud  : ")); DEBUG_SERIAL.println(activeBaud);
    DEBUG_SERIAL.print(F("LoRa baud    : ")); DEBUG_SERIAL.println(loraBaud);
    DEBUG_SERIAL.print(F("Master T3.5  : ")); DEBUG_SERIAL.print(masterTimeoutUs);
                                               DEBUG_SERIAL.println(F(" us"));
    DEBUG_SERIAL.print(F("LoRa timeout : ")); DEBUG_SERIAL.print(LORA_RESPONSE_TIMEOUT_MS);
                                               DEBUG_SERIAL.println(F(" ms"));
    DEBUG_SERIAL.print(F("Max failures : ")); DEBUG_SERIAL.println(CACHE_MAX_FAILURES);
    DEBUG_SERIAL.println(F("Waiting for master requests to learn register map..."));
    DEBUG_SERIAL.println();
#endif

    lastFrameMs = millis();
    vmDoneMs    = millis();
    digitalWrite(STATUS_LED, LOW);
}

// ============================================================
//  Main loop — dual proxy
// ============================================================
void loop() {
    uint32_t now = millis();

    // ════════════════════════════════════════════════════════════
    //  1. VIRTUAL SLAVE — collect master bytes
    // ════════════════════════════════════════════════════════════
    while (MASTER_SERIAL.available()) {
        if (masterLen < MAX_FRAME_SIZE)
            masterBuf[masterLen++] = MASTER_SERIAL.read();
        else
            MASTER_SERIAL.read();
        masterLastByteUs = micros();
    }

    // ════════════════════════════════════════════════════════════
    //  2. VIRTUAL SLAVE — handle complete master frame
    // ════════════════════════════════════════════════════════════
    if (masterLen > 0 &&
        (uint32_t)(micros() - masterLastByteUs) >= masterTimeoutUs) {

        bool crcOk = crcValid(masterBuf, masterLen);

#if CRC_CHECK_MODE >= 1
        if (!crcOk && masterLen > 0) {
            statsMasterBad++;
#if DEBUG_ENABLED
            logTs(F("M>VS [CRC ERR] "));
            printHexDump(masterBuf, masterLen);
            DEBUG_SERIAL.println();
#endif
        }
#endif

#if CRC_CHECK_MODE >= 2
        if (crcOk && masterLen == 8) {
#else
        if (masterLen == 8) {
#endif
            uint8_t func = masterBuf[1];

            if (func == 0x03 || func == 0x04) {
                statsMasterReq++;
                fwdLen = 0;
                fwdIdx = 0;

                CacheEntry *e = cacheFind(masterBuf);
                bool isNew = (e == nullptr);
                if (!e) e = cacheAdd(masterBuf);

#if DEBUG_ENABLED
                logTs(isNew ? F("M>VS [NEW]    ") : F("M>VS [req]    "));
                if (e) printReqInfo(e->request);
#endif

                if (e && cacheIsValid(e)) {
                    memcpy(fwdBuf, e->response, e->responseLen);
                    fwdLen = e->responseLen;
                    fwdIdx = 0;
                    statsCacheServed++;
#if DEBUG_ENABLED
                    DEBUG_SERIAL.print(F(" -> cached ("));
                    DEBUG_SERIAL.print(now - e->updatedMs);
                    DEBUG_SERIAL.print(F("ms old)"));
#endif
                } else {
                    statsCacheMiss++;
#if DEBUG_ENABLED
                    if (!e)                                    DEBUG_SERIAL.print(F(" -> FULL"));
                    else if (!e->hasData)                      DEBUG_SERIAL.print(F(" -> no data yet"));
                    else                                       DEBUG_SERIAL.print(F(" -> slave down"));
#endif
                }
#if DEBUG_ENABLED
                DEBUG_SERIAL.println();
#endif
            }
        }

        masterLen = 0;
    }

    // ════════════════════════════════════════════════════════════
    //  3. Non-blocking cache → master forwarding
    // ════════════════════════════════════════════════════════════
    if (fwdIdx < fwdLen) {
        uint16_t space = MASTER_SERIAL.availableForWrite();
        if (space > 0) {
            uint16_t remaining = fwdLen - fwdIdx;
            uint16_t chunk = (space < remaining) ? space : remaining;
            MASTER_SERIAL.write(fwdBuf + fwdIdx, chunk);
            fwdIdx += chunk;
        }
    }

    // ════════════════════════════════════════════════════════════
    //  4. VIRTUAL MASTER — LoRa state machine
    // ════════════════════════════════════════════════════════════

    // 4a. Drain noise while idle
    if (vmState == VM_IDLE) {
        while (LORA_SERIAL.available()) LORA_SERIAL.read();
    }

    // 4b. Collect LoRa response bytes while waiting
    if (vmState == VM_WAITING) {
        while (LORA_SERIAL.available()) {
            if (loraLen < MAX_FRAME_SIZE)
                loraBuf[loraLen++] = LORA_SERIAL.read();
            else
                LORA_SERIAL.read();
            loraLastByteUs = micros();
        }

        // — Check for complete response (expected-length + CRC) —
        if (loraLen >= 5) {
            bool     complete    = false;
            uint16_t frameLen    = 0;
            bool     isException = (loraBuf[1] & 0x80) != 0;

            if (isException) {
                frameLen = 5;
                complete = crcValid(loraBuf, 5);
            } else if (loraLen >= 3) {
                frameLen = (uint16_t)3 + loraBuf[2] + 2;
                if (frameLen <= MAX_FRAME_SIZE && loraLen >= frameLen)
                    complete = crcValid(loraBuf, frameLen);
            }

            if (complete) {
                CacheEntry *e = &cache[vmActiveIdx];

                bool addrMatch = (loraBuf[0] == e->request[0]);
                bool funcMatch = ((loraBuf[1] & 0x7F) == (e->request[1] & 0x7F));

                if (addrMatch && funcMatch && !isException) {
                    memcpy(e->response, loraBuf, frameLen);
                    e->responseLen = frameLen;
                    e->updatedMs   = now;
                    e->failCount   = 0;
                    e->hasData     = true;
                    statsLoraOk++;
#if DEBUG_ENABLED
                    logTs(F("L>VM [OK]     "));
                    printReqInfo(e->request);
                    DEBUG_SERIAL.print(F(" Resp="));
                    DEBUG_SERIAL.print(frameLen);
                    DEBUG_SERIAL.println('B');
                    printDecodedResponse(e->request, loraBuf, frameLen);
#endif
                } else if (addrMatch && funcMatch && isException) {
                    e->failCount++;
                    statsLoraExcept++;
#if DEBUG_ENABLED
                    logTs(F("L>VM [EXCEPT] "));
                    printReqInfo(e->request);
                    DEBUG_SERIAL.print(F(" Exc="));
                    DEBUG_SERIAL.println(loraBuf[2]);
#endif
                } else {
#if DEBUG_ENABLED
                    logTs(F("L>VM [mismatch] "));
                    printHexDump(loraBuf, frameLen);
                    DEBUG_SERIAL.println();
#endif
                }

                loraLen = 0;
                vmState = VM_IDLE;
                vmDoneMs = now;
                digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            }
        }

        // — Gap timeout: discard corrupt / incomplete data —
        if (loraLen > 0 &&
            (uint32_t)(micros() - loraLastByteUs) >= LORA_FRAME_TIMEOUT_US) {
            cache[vmActiveIdx].failCount++;
            statsLoraCrcErr++;
#if DEBUG_ENABLED
            logTs(F("L>VM [corrupt] "));
            printHexDump(loraBuf, loraLen);
            DEBUG_SERIAL.println();
#endif
            loraLen  = 0;
            vmState  = VM_IDLE;
            vmDoneMs = now;
        }

        // — Overall response timeout: slave never answered —
        if (vmState == VM_WAITING &&
            (now - vmSentMs) >= LORA_RESPONSE_TIMEOUT_MS) {
            cache[vmActiveIdx].failCount++;
            statsLoraTimeout++;
#if DEBUG_ENABLED
            logTs(F("L>VM [TIMEOUT] "));
            printReqInfo(cache[vmActiveIdx].request);
            DEBUG_SERIAL.print(F(" fails="));
            DEBUG_SERIAL.println(cache[vmActiveIdx].failCount);
#endif
            loraLen  = 0;
            vmState  = VM_IDLE;
            vmDoneMs = now;
        }
    }

    // 4c. IDLE → pick next entry and send request to LoRa
    if (vmState == VM_IDLE && cacheCount > 0 &&
        (now - vmDoneMs) >= VM_INTER_REQUEST_MS) {

        vmActiveIdx = vmPollIdx % cacheCount;
        vmPollIdx++;

        while (LORA_SERIAL.available()) LORA_SERIAL.read();
        loraLen = 0;

        LORA_SERIAL.write(cache[vmActiveIdx].request, 8);
        LORA_SERIAL.flush();

        vmSentMs = now;
        vmState  = VM_WAITING;
        statsLoraReq++;

#if DEBUG_ENABLED
        logTs(F("VM>L [poll]   "));
        printReqInfo(cache[vmActiveIdx].request);
        DEBUG_SERIAL.println();
#endif
    }

    // ════════════════════════════════════════════════════════════
    //  5. Periodic stats
    // ════════════════════════════════════════════════════════════
#if DEBUG_ENABLED
    static uint32_t lastStats = 0;
    if ((now - lastStats) >= STATS_INTERVAL_MS) {
        logStats();
        lastStats = now;
    }
#endif
}
