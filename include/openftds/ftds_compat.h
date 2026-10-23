/* SPDX-License-Identifier: MIT */
#ifndef OPENFTDS_FTDS_COMPAT_H
#define OPENFTDS_FTDS_COMPAT_H

#include "openftds.h"

/* Source-level helpers matching the FTDS calls visible in public projects. */
#define TRACE_LEVEL_MODULE_NORMAL OPENFTDS_LEVEL_NORMAL
#define TRACE_LEVEL_MODULE_HIGH OPENFTDS_LEVEL_HIGH
#define STAT_MODE_DELAY 0

static inline int initFtdsClient(void) { return openftds_init(NULL); }

void openftds_compat_delay_start(uint32_t event_id, openftds_level_t level,
                                 void *token);
void openftds_compat_delay_end(const char *name, const char *module,
                               uint32_t event_id, openftds_level_t level,
                               const void *value, int status);

static inline openftds_level_t openftds_compat_level(uint32_t event_id) {
    unsigned encoded = event_id >> 24;
    return encoded <= (unsigned)OPENFTDS_LEVEL_CRITICAL
               ? (openftds_level_t)encoded
               : OPENFTDS_LEVEL_NORMAL;
}

#define FTDS_DELAY_START(NAME, MODULE, ID, MODE, TOKEN_PTR)                 \
    do {                                                                   \
        (void)(MODE);                                                      \
        openftds_compat_delay_start((ID), openftds_compat_level((ID)),      \
                                    (void *)(TOKEN_PTR));                  \
    } while (0)

#define FTDS_DELAY_END(NAME, MODULE, ID, MODE, TOKEN_PTR, STATUS)          \
    do {                                                                   \
        (void)(MODE);                                                      \
        openftds_compat_delay_end((NAME), (MODULE), (ID),                  \
                                  openftds_compat_level((ID)),             \
                                  (const void *)(TOKEN_PTR),               \
                                  (STATUS));                               \
    } while (0)

#endif
