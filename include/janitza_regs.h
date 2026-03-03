#pragma once
#include <avr/pgmspace.h>

// Janitza UMG 104 register map (subset: registers read by our master)
// All values are IEEE 754 float, 2 registers (4 bytes) each, big-endian.

struct RegName {
    uint16_t    addr;
    const char *name;   // PROGMEM string
};

// Short names to keep flash usage low
static const char RN_UL1N[]  PROGMEM = "UL1N";
static const char RN_UL2N[]  PROGMEM = "UL2N";
static const char RN_UL3N[]  PROGMEM = "UL3N";
static const char RN_UL12[]  PROGMEM = "UL12";
static const char RN_UL23[]  PROGMEM = "UL23";
static const char RN_UL31[]  PROGMEM = "UL31";
static const char RN_IL1[]   PROGMEM = "IL1";
static const char RN_IL2[]   PROGMEM = "IL2";
static const char RN_IL3[]   PROGMEM = "IL3";
static const char RN_ISUM[]  PROGMEM = "Isum";
static const char RN_PL1[]   PROGMEM = "PL1";
static const char RN_PL2[]   PROGMEM = "PL2";
static const char RN_PL3[]   PROGMEM = "PL3";
static const char RN_PSUM[]  PROGMEM = "Psum";
static const char RN_SL1[]   PROGMEM = "SL1";
static const char RN_SL2[]   PROGMEM = "SL2";
static const char RN_SL3[]   PROGMEM = "SL3";
static const char RN_SSUM[]  PROGMEM = "Ssum";
static const char RN_QL1[]   PROGMEM = "QL1";
static const char RN_QL2[]   PROGMEM = "QL2";
static const char RN_QL3[]   PROGMEM = "QL3";
static const char RN_QSUM[]  PROGMEM = "Qsum";
static const char RN_PF1[]   PROGMEM = "PF1";
static const char RN_PF2[]   PROGMEM = "PF2";
static const char RN_PF3[]   PROGMEM = "PF3";
static const char RN_FREQ[]  PROGMEM = "Freq";
static const char RN_PHSEQ[] PROGMEM = "PhSeq";
static const char RN_WHL1[]  PROGMEM = "WhL1";
static const char RN_WHL2[]  PROGMEM = "WhL2";
static const char RN_WHL3[]  PROGMEM = "WhL3";
static const char RN_WHSUM[] PROGMEM = "WhSum";
static const char RN_WVC1[]  PROGMEM = "WhC1";
static const char RN_WVC2[]  PROGMEM = "WhC2";
static const char RN_WVC3[]  PROGMEM = "WhC3";
static const char RN_WVCS[]  PROGMEM = "WhCons";
static const char RN_WZL1[]  PROGMEM = "WhD1";
static const char RN_WZL2[]  PROGMEM = "WhD2";
static const char RN_WZL3[]  PROGMEM = "WhD3";
static const char RN_WZSUM[] PROGMEM = "WhDlvr";

static const RegName JANITZA_REGS[] PROGMEM = {
    {19000, RN_UL1N},  {19002, RN_UL2N},  {19004, RN_UL3N},
    {19006, RN_UL12},  {19008, RN_UL23},  {19010, RN_UL31},
    {19012, RN_IL1},   {19014, RN_IL2},   {19016, RN_IL3},
    {19018, RN_ISUM},
    {19020, RN_PL1},   {19022, RN_PL2},   {19024, RN_PL3},
    {19026, RN_PSUM},
    {19028, RN_SL1},   {19030, RN_SL2},   {19032, RN_SL3},
    {19034, RN_SSUM},
    {19036, RN_QL1},   {19038, RN_QL2},   {19040, RN_QL3},
    {19042, RN_QSUM},
    {19044, RN_PF1},   {19046, RN_PF2},   {19048, RN_PF3},
    {19050, RN_FREQ},  {19052, RN_PHSEQ},
    {19054, RN_WHL1},  {19056, RN_WHL2},  {19058, RN_WHL3},
    {19060, RN_WHSUM},
    {19062, RN_WVC1},  {19064, RN_WVC2},  {19066, RN_WVC3},
    {19068, RN_WVCS},
    {19070, RN_WZL1},  {19072, RN_WZL2},  {19074, RN_WZL3},
    {19076, RN_WZSUM},
};

#define JANITZA_REG_COUNT (sizeof(JANITZA_REGS) / sizeof(JANITZA_REGS[0]))
