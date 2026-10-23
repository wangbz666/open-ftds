/*
 * Copyright 2026 OpenFTDS contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef OPENFTDS_OPENFTDS_H
#define OPENFTDS_OPENFTDS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENFTDS_VERSION_MAJOR 0
#define OPENFTDS_VERSION_MINOR 2
#define OPENFTDS_VERSION_PATCH 1

typedef enum openftds_level {
    OPENFTDS_LEVEL_NORMAL = 0,
    OPENFTDS_LEVEL_HIGH = 1,
    OPENFTDS_LEVEL_CRITICAL = 2
} openftds_level_t;

typedef struct openftds_config {
    /* Root containing data/stat and run/client. NULL uses OPENFTDS_DIR or ./openftds. */
    const char *directory;
    /* Size of one .ios file before rotation. Zero uses 20 MiB. */
    size_t rotate_bytes;
    /* Number of files retained for this process. Zero uses 50. */
    unsigned max_files;
    /* Periodic aggregation interval. Zero uses 10 seconds. */
    unsigned flush_interval_ms;
    /* Events below this level are ignored. */
    openftds_level_t min_level;
    /* Call fsync(2) after every flush. Usually unnecessary. */
    int fsync_on_flush;
    /* Record one out of every N operations. Zero and one both mean all. */
    unsigned sample_rate;
    /* Count operations at or above this duration as slow. Zero disables it. */
    uint64_t slow_threshold_ns;
} openftds_config_t;

typedef struct openftds_token {
    uint64_t start_ns;
    long pid;
} openftds_token_t;

typedef struct openftds_status {
    long pid;
    int initialized;
    int enabled;
    openftds_level_t min_level;
    unsigned sample_rate;
    uint64_t slow_threshold_ns;
    unsigned registered_events;
} openftds_status_t;

/* Initializes the process-wide collector. Safe to call more than once. */
int openftds_init(const openftds_config_t *config);

/* Flushes interval aggregates and releases resources. Safe to call more than once. */
void openftds_shutdown(void);

/* Writes the current interval to disk without stopping the collector. */
int openftds_flush(void);

/* Runtime controls. Changes affect subsequent measurements. */
void openftds_set_enabled(int enabled);
int openftds_is_enabled(void);
void openftds_set_min_level(openftds_level_t min_level);
void openftds_set_sample_rate(unsigned one_in_n);

/* Returns process-local diagnostic state without flushing aggregates. */
int openftds_get_status(openftds_status_t *status);

/* Monotonic timestamp used by the collector. */
uint64_t openftds_now_ns(void);

/* Starts a duration measurement. Disabled levels return a zero token. */
openftds_token_t openftds_delay_start(openftds_level_t level);

/* Completes and records a duration measurement. status == 0 means success. */
void openftds_delay_end(const char *name,
                        const char *module,
                        uint32_t event_id,
                        openftds_level_t level,
                        openftds_token_t token,
                        int status);

/* Records a precomputed duration, useful for external clocks and tests. */
void openftds_record(const char *name,
                     const char *module,
                     uint32_t event_id,
                     openftds_level_t level,
                     uint64_t duration_ns,
                     int status);

/* C macros intentionally mirror a start/end instrumentation style. */
#define OPENFTDS_DELAY_START(LEVEL, TOKEN) \
    do { (TOKEN) = openftds_delay_start((LEVEL)); } while (0)

#define OPENFTDS_DELAY_END(NAME, MODULE, ID, LEVEL, TOKEN, STATUS) \
    do { openftds_delay_end((NAME), (MODULE), (ID), (LEVEL), (TOKEN), (STATUS)); } while (0)

#ifdef __cplusplus
}
#endif

#endif
