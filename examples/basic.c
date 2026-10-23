/* SPDX-License-Identifier: MIT */
#include "openftds/openftds.h"

#include <stdio.h>
#include <time.h>

static int simulated_read(void) {
    openftds_token_t token;
    OPENFTDS_DELAY_START(OPENFTDS_LEVEL_HIGH, token);

    struct timespec delay = {.tv_sec = 0, .tv_nsec = 2 * 1000 * 1000};
    nanosleep(&delay, NULL);
    int status = 0;

    OPENFTDS_DELAY_END("CACHE_READ", "cache", 447,
                       OPENFTDS_LEVEL_HIGH, token, status);
    return status;
}

int main(void) {
    openftds_config_t config = {
        .directory = "./demo-output",
        .flush_interval_ms = 1000,
    };
    int rc = openftds_init(&config);
    if (rc != 0) {
        fprintf(stderr, "openftds_init failed: %d\n", rc);
        return 1;
    }

    for (int i = 0; i < 100; ++i) simulated_read();
    openftds_shutdown();
    puts("Run: openftds-report ./demo-output");
    return 0;
}
