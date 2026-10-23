/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L

#include <ftds.h>

#include <stdint.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(void) {
    if (initFtdsClient() != 0) return 1;

    const uint32_t event_id = (1u << 24) | (6u << 16) | 447u;
    uint64_t started = now_ns();
    FTDS_DELAY_START("READ_TOTAL", NULL, event_id, STAT_MODE_DELAY, NULL);

    /* Replace this block with the actual operation. */
    struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
    (void)nanosleep(&pause, NULL);
    int status = 0;

    uint64_t duration = now_ns() - started;
    FTDS_DELAY_END("READ_TOTAL", NULL, event_id, STAT_MODE_DELAY,
                   &duration, status);
    openftds_shutdown();
    return 0;
}
