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
#include <sys/wait.h>
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
    int concurrent = 0, compat = 0, precomputed = 0;
    int child = 0, parent = 0, enabled = 0, disabled = 0, sampled = 0;
    int inherited_token = 0;
    char line[16384];
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".ios")) continue;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", stat_dir, entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, "\"event_id\":9001") &&
                strstr(line, "\"count\":40000") &&
                strstr(line, "\"errors\":400") &&
                strstr(line, "\"slow\":40000")) concurrent = 1;
            if (strstr(line, "\"event_id\":9002")) compat = 1;
            if (strstr(line, "\"event_id\":17170880") &&
                strstr(line, "\"total_ns\":1234567")) precomputed = 1;
            if (strstr(line, "\"event_id\":9003")) child = 1;
            if (strstr(line, "\"event_id\":9004")) parent = 1;
            if (strstr(line, "\"event_id\":9005")) disabled = 1;
            if (strstr(line, "\"event_id\":9006")) enabled = 1;
            if (strstr(line, "\"event_id\":9007") &&
                strstr(line, "\"count\":5")) sampled = 1;
            if (strstr(line, "\"event_id\":9008")) inherited_token = 1;
        }
        fclose(file);
    }
    closedir(dir);
    return concurrent && compat && precomputed && child && parent &&
           enabled && !disabled && sampled && !inherited_token ? 0 : EINVAL;
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
        .slow_threshold_ns = 1000,
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

    openftds_set_enabled(0);
    openftds_record("DISABLED", "test", 9005, OPENFTDS_LEVEL_NORMAL, 2000, 0);
    openftds_set_enabled(1);
    openftds_record("ENABLED", "test", 9006, OPENFTDS_LEVEL_NORMAL, 2000, 0);

    openftds_set_sample_rate(2);
    for (int i = 0; i < 10; ++i)
        openftds_record("SAMPLED", "test", 9007, OPENFTDS_LEVEL_NORMAL, 2000, 0);
    openftds_set_sample_rate(1);

    openftds_status_t status;
    if (openftds_get_status(&status) != 0 || !status.initialized ||
        status.pid != (long)getpid() || status.sample_rate != 1) return 4;

    openftds_token_t parent_token = openftds_delay_start(OPENFTDS_LEVEL_NORMAL);
    pid_t child_pid = fork();
    if (child_pid < 0) return 5;
    if (child_pid == 0) {
        openftds_delay_end("INHERITED_TOKEN", "test", 9008,
                           OPENFTDS_LEVEL_NORMAL, parent_token, 0);
        openftds_record("FORK_CHILD", "test", 9003,
                        OPENFTDS_LEVEL_NORMAL, 3000, 0);
        if (openftds_flush() != 0) _exit(20);
        openftds_shutdown();
        _exit(0);
    }
    int child_status = 0;
    if (waitpid(child_pid, &child_status, 0) != child_pid ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) return 6;
    openftds_record("FORK_PARENT", "test", 9004,
                    OPENFTDS_LEVEL_NORMAL, 4000, 0);
    if (openftds_flush() != 0) return 4;
    openftds_shutdown();
    int result = verify_output(directory);
    if (result != 0) fprintf(stderr, "output verification failed: %d\n", result);
    return result == 0 ? 0 : 5;
}
