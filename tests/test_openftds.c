/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L

#include "openftds/ftds_compat.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { THREADS = 4, RECORDS = 10000 };

static void *record_many(void *unused) {
    (void)unused;
    for (int i = 0; i < RECORDS; ++i) {
        openftds_record("CONCURRENT", "test", 9001, OPENFTDS_LEVEL_HIGH,
                        (uint64_t)(1000 + i), i % 100 == 0 ? -1 : 0);
    }
    return NULL;
}

static int verify_output(const char *directory) {
    char stat_dir[1024];
    snprintf(stat_dir, sizeof(stat_dir), "%s/data/stat", directory);
    DIR *dir = opendir(stat_dir);
    if (!dir) return errno;
    char content[16384] = {0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".ios")) continue;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", stat_dir, entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        fread(content, 1, sizeof(content) - 1, file);
        fclose(file);
    }
    closedir(dir);
    return strstr(content, "\"event_id\":9001") &&
           strstr(content, "\"count\":40000") &&
           strstr(content, "\"errors\":400") &&
           strstr(content, "\"event_id\":9002") &&
           strstr(content, "\"event_id\":17170880") &&
           strstr(content, "\"total_ns\":1234567") ? 0 : EINVAL;
}

int main(void) {
    char directory[256];
    snprintf(directory, sizeof(directory), "/tmp/openftds-test-%ld-%ld",
             (long)getpid(), (long)time(NULL));
    if (mkdir(directory, 0700) != 0) return 1;
    openftds_config_t config = {
        .directory = directory,
        .rotate_bytes = 1024 * 1024,
        .max_files = 3,
        .flush_interval_ms = 60000,
    };
    if (openftds_init(&config) != 0) return 2;

    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i) {
        if (pthread_create(&threads[i], NULL, record_many, NULL) != 0) return 3;
    }
    for (int i = 0; i < THREADS; ++i) pthread_join(threads[i], NULL);
    FTDS_DELAY_START("COMPAT", "test", 9002, STAT_MODE_DELAY, NULL);
    FTDS_DELAY_END("COMPAT", "test", 9002, STAT_MODE_DELAY, NULL, 0);
    uint64_t external_delay = 1234567;
    FTDS_DELAY_START("PRECOMPUTED", NULL, 0x010601c0u,
                     STAT_MODE_DELAY, NULL);
    FTDS_DELAY_END("PRECOMPUTED", NULL, 0x010601c0u,
                   STAT_MODE_DELAY, &external_delay, -1);
    if (openftds_flush() != 0) return 4;
    openftds_shutdown();
    int result = verify_output(directory);
    if (result != 0) fprintf(stderr, "output verification failed: %d\n", result);
    return result == 0 ? 0 : 5;
}
