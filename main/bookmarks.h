/*
 * bookmarks.h — preset frequencies for the NYC area.
 *
 * All are conventional ANALOG narrowband FM (mode=1), receivable by the NBFM
 * demod. NOAA weather is rock-solid; the FDNY fire / EMS UHF dispatch channels
 * are from public scanner references and may change — verify against a current
 * source. NYPD and some agencies use P25 digital trunked radio, which this
 * simple FM demodulator cannot decode.
 *
 * FDNY EMS borough coverage here is partial (the full per-borough list wasn't
 * confidently sourced); extend BM_EMS as you confirm channels.
 */
#pragma once
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t hz;
    uint8_t nfm; /* 1 = narrowband FM */
} bookmark_t;

/* NOAA Weather Radio (162 MHz), NYC primary is 162.550 (KWO35). */
static const bookmark_t BM_WX[] = {
    {"NOAA 162.550 (NYC)", 162550000, 1},
    {"NOAA 162.400", 162400000, 1},
    {"NOAA 162.425", 162425000, 1},
    {"NOAA 162.450", 162450000, 1},
    {"NOAA 162.475", 162475000, 1},
    {"NOAA 162.500", 162500000, 1},
    {"NOAA 162.525", 162525000, 1},
};

/* FDNY fire dispatch (UHF, analog). */
static const bookmark_t BM_FDNY[] = {
    {"FDNY Citywide 1", 482231250, 1},
    {"FDNY Citywide 2", 483381250, 1},
    {"FDNY Manhattan", 482106250, 1},
    {"FDNY Brooklyn", 482018750, 1},
    {"FDNY Bronx", 482006250, 1},
    {"FDNY Queens", 482031250, 1},
    {"FDNY Staten Is.", 482043750, 1},
};

/* FDNY EMS dispatch (UHF, analog) — partial list, verify/extend. */
static const bookmark_t BM_EMS[] = {
    {"EMS Manhattan N", 482756250, 1},
    {"EMS Manhattan C", 482981250, 1},
    {"EMS Brooklyn C", 482506250, 1},
    {"EMS Bronx N", 483206250, 1},
};

#define BM_WX_N   (int)(sizeof(BM_WX) / sizeof(BM_WX[0]))
#define BM_FDNY_N (int)(sizeof(BM_FDNY) / sizeof(BM_FDNY[0]))
#define BM_EMS_N  (int)(sizeof(BM_EMS) / sizeof(BM_EMS[0]))
