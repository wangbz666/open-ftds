/*
 * Copyright 2026 OpenFTDS contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "openftds/ftds_compat.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define OPENFTDS_MAX_EVENTS 4096u
#define OPENFTDS_HIST_BUCKETS 32u
#define OPENFTDS_NAME_MAX 95u
#define OPENFTDS_MODULE_MAX 47u
#define OPENFTDS_DEFAULT_ROTATE (20u * 1024u * 1024u)
#define OPENFTDS_DEFAULT_FILES 50u
#define OPENFTDS_DEFAULT_INTERVAL 10000u
#define OPENFTDS_COMPAT_STACK 64u

typedef struct event_slot {
    atomic_uint state; /* 0 empty, 1 initializing, 2 ready */
    atomic_flag lock;
    uint32_t id;
    openftds_level_t level;
    char name[OPENFTDS_NAME_MAX + 1];
    char module[OPENFTDS_MODULE_MAX + 1];
    uint64_t count;
    uint64_t errors;
    uint64_t slow;
    uint64_t total_ns;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t hist[OPENFTDS_HIST_BUCKETS];
} event_slot_t;

typedef struct collector {
    pthread_mutex_t lifecycle_mu;
    pthread_mutex_t flush_mu;
    pthread_mutex_t registry_mu;
    pthread_cond_t wake_cv;
    pthread_t writer_thread;
    int writer_started;
    int initialized;
    int stopping;
    atomic_long owner_pid;
    atomic_int enabled;
    atomic_int min_level;
    atomic_uint sample_rate;
    openftds_config_t config;
    openftds_config_t fork_config;
    int fork_config_pending;
    int fork_enabled;
    char root[PATH_MAX];
    char fork_root[PATH_MAX];
    char stat_dir[PATH_MAX];
    char log_dir[PATH_MAX];
    char file_stem[160];
    FILE *out;
    FILE *log;
    int out_fd;
    int log_fd;
    unsigned sequence;
    uint64_t started_epoch;
    event_slot_t slots[OPENFTDS_MAX_EVENTS];
} collector_t;

static collector_t g = {
    .lifecycle_mu = PTHREAD_MUTEX_INITIALIZER,
    .flush_mu = PTHREAD_MUTEX_INITIALIZER,
    .registry_mu = PTHREAD_MUTEX_INITIALIZER,
    .wake_cv = PTHREAD_COND_INITIALIZER,
    .owner_pid = ATOMIC_VAR_INIT(0),
    .enabled = ATOMIC_VAR_INIT(1),
    .min_level = ATOMIC_VAR_INIT(OPENFTDS_LEVEL_NORMAL),
    .sample_rate = ATOMIC_VAR_INIT(1),
    .out_fd = -1,
    .log_fd = -1
};

typedef struct compat_frame {
    uint32_t event_id;
    openftds_level_t level;
    openftds_token_t token;
    int caller_token;
} compat_frame_t;

static _Thread_local compat_frame_t compat_frames[OPENFTDS_COMPAT_STACK];
static _Thread_local unsigned compat_depth;
static _Thread_local uint64_t sample_counter;

static void reset_sync_primitives_after_fork(void) {
    pthread_mutex_t lifecycle = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t flush = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t registry = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
    memcpy(&g.lifecycle_mu, &lifecycle, sizeof(lifecycle));
    memcpy(&g.flush_mu, &flush, sizeof(flush));
    memcpy(&g.registry_mu, &registry, sizeof(registry));
    memcpy(&g.wake_cv, &wake, sizeof(wake));
}

static void reset_slots_after_fork(void) {
    memset(g.slots, 0, sizeof(g.slots));
    for (unsigned i = 0; i < OPENFTDS_MAX_EVENTS; ++i) {
        atomic_init(&g.slots[i].state, 0);
        atomic_flag_clear_explicit(&g.slots[i].lock, memory_order_release);
    }
}

/*
 * A vLLM worker may fork after the collector has started. Only the calling
 * thread survives, so inherited mutex and stdio state cannot be reused. A PID
 * change is detected before any collector lock is touched; the child drops
 * parent aggregates and lazily starts a new writer with the inherited config.
 */
static void recover_after_fork_if_needed(void) {
    long current = (long)getpid();
    for (;;) {
        long owner = atomic_load_explicit(&g.owner_pid, memory_order_acquire);
        if (owner == current) return;
        if (owner == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &g.owner_pid, &owner, current,
                    memory_order_acq_rel, memory_order_acquire)) return;
            continue;
        }
        if (owner == -current) continue;
        long expected = owner;
        if (!atomic_compare_exchange_weak_explicit(
                &g.owner_pid, &expected, -current,
                memory_order_acq_rel, memory_order_acquire)) continue;

        int preserve_config = g.initialized;
        if (preserve_config) {
            g.fork_config = g.config;
            g.fork_config.min_level = (openftds_level_t)atomic_load_explicit(
                &g.min_level, memory_order_relaxed);
            g.fork_config.sample_rate = atomic_load_explicit(
                &g.sample_rate, memory_order_relaxed);
            g.fork_enabled = atomic_load_explicit(
                &g.enabled, memory_order_relaxed);
            (void)snprintf(g.fork_root, sizeof(g.fork_root), "%s", g.root);
        }
        if (g.out_fd >= 0) (void)close(g.out_fd);
        if (g.log_fd >= 0 && g.log_fd != g.out_fd) (void)close(g.log_fd);
        g.out = NULL;
        g.log = NULL;
        g.out_fd = -1;
        g.log_fd = -1;
        g.writer_started = 0;
        g.initialized = 0;
        g.stopping = 0;
        g.sequence = 0;
        g.started_epoch = 0;
        g.fork_config_pending = preserve_config;
        reset_sync_primitives_after_fork();
        reset_slots_after_fork();
        compat_depth = 0;
        sample_counter = 0;
        atomic_store_explicit(&g.owner_pid, current, memory_order_release);
        return;
    }
}

static void lock_slot(event_slot_t *slot) {
    while (atomic_flag_test_and_set_explicit(&slot->lock, memory_order_acquire)) {
        /* Expected to be held only for a few integer operations. */
    }
}

static void unlock_slot(event_slot_t *slot) {
    atomic_flag_clear_explicit(&slot->lock, memory_order_release);
}

uint64_t openftds_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t epoch_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int mkdirs(const char *path) {
    char temp[PATH_MAX];
    size_t length = strlen(path);
    if (length == 0 || length >= sizeof(temp)) return ENAMETOOLONG;
    memcpy(temp, path, length + 1);
    for (char *p = temp + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(temp, 0750) != 0 && errno != EEXIST) return errno;
        *p = '/';
    }
    if (mkdir(temp, 0750) != 0 && errno != EEXIST) return errno;
    return 0;
}

static void sanitize_component(char *value) {
    for (; *value; ++value) {
        char c = *value;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            *value = '_';
        }
    }
}

static void log_message(const char *format, ...) {
    if (!g.log) return;
    fprintf(g.log, "%llu ", (unsigned long long)epoch_ns());
    va_list args;
    va_start(args, format);
    vfprintf(g.log, format, args);
    va_end(args);
    fputc('\n', g.log);
    fflush(g.log);
}

static int open_output(void) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s-%06u.ios",
                           g.stat_dir, g.file_stem, g.sequence);
    if (written < 0 || (size_t)written >= sizeof(path)) return ENAMETOOLONG;
    g.out = fopen(path, "a");
    if (!g.out) return errno;
    g.out_fd = fileno(g.out);
    setvbuf(g.out, NULL, _IOFBF, 64u * 1024u);
    return 0;
}

static void retain_files(void) {
    if (g.config.max_files == 0 || g.sequence < g.config.max_files) return;
    unsigned old_sequence = g.sequence - g.config.max_files;
    char path[PATH_MAX];
    int written = snprintf(path, sizeof(path), "%s/%s-%06u.ios",
                           g.stat_dir, g.file_stem, old_sequence);
    if (written > 0 && (size_t)written < sizeof(path)) (void)unlink(path);
}

static int rotate_if_needed(void) {
    if (!g.out) return open_output();
    long position = ftell(g.out);
    if (position < 0 || (size_t)position < g.config.rotate_bytes) return 0;
    fclose(g.out);
    g.out = NULL;
    g.out_fd = -1;
    ++g.sequence;
    retain_files();
    return open_output();
}

static void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        switch (*p) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if (*p < 0x20) fprintf(out, "\\u%04x", (unsigned)*p);
                else fputc(*p, out);
        }
    }
    fputc('"', out);
}

static unsigned histogram_bucket(uint64_t duration_ns) {
    uint64_t microseconds = (duration_ns + 999u) / 1000u;
    unsigned bucket = 0;
    while (microseconds > 1 && bucket + 1 < OPENFTDS_HIST_BUCKETS) {
        microseconds = (microseconds + 1) >> 1;
        ++bucket;
    }
    return bucket;
}

static uint32_t hash_id(uint32_t id) {
    id ^= id >> 16;
    id *= 0x7feb352du;
    id ^= id >> 15;
    id *= 0x846ca68bu;
    return id ^ (id >> 16);
}

static event_slot_t *lookup_slot(uint32_t id) {
    uint32_t start = hash_id(id) % OPENFTDS_MAX_EVENTS;
    for (uint32_t probe = 0; probe < OPENFTDS_MAX_EVENTS; ++probe) {
        event_slot_t *slot = &g.slots[(start + probe) % OPENFTDS_MAX_EVENTS];
        unsigned state = atomic_load_explicit(&slot->state, memory_order_acquire);
        if (state == 2 && slot->id == id) return slot;
    }
    return NULL;
}

static event_slot_t *find_slot(uint32_t id, const char *name, const char *module,
                               openftds_level_t level) {
    event_slot_t *existing = lookup_slot(id);
    if (existing) return existing;

    pthread_mutex_lock(&g.registry_mu);
    existing = lookup_slot(id);
    if (existing) {
        pthread_mutex_unlock(&g.registry_mu);
        return existing;
    }
    uint32_t start = hash_id(id) % OPENFTDS_MAX_EVENTS;
    for (uint32_t probe = 0; probe < OPENFTDS_MAX_EVENTS; ++probe) {
        event_slot_t *slot = &g.slots[(start + probe) % OPENFTDS_MAX_EVENTS];
        if (atomic_load_explicit(&slot->state, memory_order_acquire) != 0) continue;
        atomic_store_explicit(&slot->state, 1, memory_order_release);
        slot->id = id;
        slot->level = level;
        snprintf(slot->name, sizeof(slot->name), "%s", name ? name : "");
        snprintf(slot->module, sizeof(slot->module), "%s", module ? module : "");
        slot->min_ns = UINT64_MAX;
        atomic_flag_clear_explicit(&slot->lock, memory_order_release);
        atomic_store_explicit(&slot->state, 2, memory_order_release);
        pthread_mutex_unlock(&g.registry_mu);
        return slot;
    }
    pthread_mutex_unlock(&g.registry_mu);
    log_message("event table full; dropped event id=%u", id);
    return NULL;
}

static int write_snapshot(void) {
    int rc = rotate_if_needed();
    if (rc != 0) return rc;
    uint64_t captured = epoch_ns();
    for (uint32_t i = 0; i < OPENFTDS_MAX_EVENTS; ++i) {
        event_slot_t *slot = &g.slots[i];
        if (atomic_load_explicit(&slot->state, memory_order_acquire) != 2) continue;

        uint64_t count, errors, slow, total_ns, min_ns, max_ns;
        uint64_t hist[OPENFTDS_HIST_BUCKETS];
        lock_slot(slot);
        count = slot->count;
        if (count == 0) {
            unlock_slot(slot);
            continue;
        }
        errors = slot->errors;
        slow = slot->slow;
        total_ns = slot->total_ns;
        min_ns = slot->min_ns;
        max_ns = slot->max_ns;
        memcpy(hist, slot->hist, sizeof(hist));
        slot->count = 0;
        slot->errors = 0;
        slot->slow = 0;
        slot->total_ns = 0;
        slot->min_ns = UINT64_MAX;
        slot->max_ns = 0;
        memset(slot->hist, 0, sizeof(slot->hist));
        unlock_slot(slot);

        fprintf(g.out, "{\"schema\":1,\"captured_at_ns\":%llu,\"pid\":%ld,\"event_id\":%u,",
                (unsigned long long)captured, (long)getpid(), slot->id);
        fputs("\"module\":", g.out); json_string(g.out, slot->module);
        fputs(",\"name\":", g.out); json_string(g.out, slot->name);
        fprintf(g.out, ",\"level\":%u,\"count\":%llu,\"errors\":%llu,"
                       "\"slow\":%llu,\"slow_threshold_ns\":%llu,"
                       "\"total_ns\":%llu,\"min_ns\":%llu,\"max_ns\":%llu,\"hist_us_log2\":[",
                (unsigned)slot->level, (unsigned long long)count,
                (unsigned long long)errors, (unsigned long long)slow,
                (unsigned long long)g.config.slow_threshold_ns,
                (unsigned long long)total_ns,
                (unsigned long long)min_ns, (unsigned long long)max_ns);
        for (unsigned b = 0; b < OPENFTDS_HIST_BUCKETS; ++b) {
            fprintf(g.out, "%s%llu", b ? "," : "", (unsigned long long)hist[b]);
        }
        fputs("]}\n", g.out);
    }
    if (fflush(g.out) != 0) return errno;
    if (g.config.fsync_on_flush && fsync(fileno(g.out)) != 0) return errno;
    return rotate_if_needed();
}

int openftds_flush(void) {
    recover_after_fork_if_needed();
    pthread_mutex_lock(&g.lifecycle_mu);
    int initialized = g.initialized;
    pthread_mutex_unlock(&g.lifecycle_mu);
    if (!initialized) return 0;
    pthread_mutex_lock(&g.flush_mu);
    int rc = write_snapshot();
    if (rc != 0) log_message("flush failed: %s", strerror(rc));
    pthread_mutex_unlock(&g.flush_mu);
    return rc;
}

static void add_ms(struct timespec *time, unsigned milliseconds) {
    time->tv_sec += (time_t)(milliseconds / 1000u);
    time->tv_nsec += (long)(milliseconds % 1000u) * 1000000L;
    if (time->tv_nsec >= 1000000000L) {
        ++time->tv_sec;
        time->tv_nsec -= 1000000000L;
    }
}

static void *writer_main(void *unused) {
    (void)unused;
    pthread_mutex_lock(&g.lifecycle_mu);
    while (!g.stopping) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        add_ms(&deadline, g.config.flush_interval_ms);
        (void)pthread_cond_timedwait(&g.wake_cv, &g.lifecycle_mu, &deadline);
        if (g.stopping) break;
        pthread_mutex_unlock(&g.lifecycle_mu);
        (void)openftds_flush();
        pthread_mutex_lock(&g.lifecycle_mu);
    }
    pthread_mutex_unlock(&g.lifecycle_mu);
    return NULL;
}

static void shutdown_at_exit(void) { openftds_shutdown(); }

static unsigned env_unsigned(const char *name, unsigned fallback) {
    const char *value = getenv(name);
    if (!value || !*value) return fallback;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (!end || *end != '\0' || parsed > UINT_MAX) return fallback;
    return (unsigned)parsed;
}

static int env_enabled(int fallback) {
    const char *value = getenv("OPENFTDS_ENABLED");
    if (!value || !*value) return fallback;
    return !(strcmp(value, "0") == 0 || strcmp(value, "off") == 0 ||
             strcmp(value, "false") == 0 || strcmp(value, "no") == 0);
}

int openftds_init(const openftds_config_t *config) {
    recover_after_fork_if_needed();
    pthread_mutex_lock(&g.lifecycle_mu);
    if (g.initialized) {
        pthread_mutex_unlock(&g.lifecycle_mu);
        return 0;
    }

    openftds_config_t selected = {0};
    char selected_root[PATH_MAX] = {0};
    int inherited = !config && g.fork_config_pending;
    if (config) {
        selected = *config;
    } else if (inherited) {
        selected = g.fork_config;
        (void)snprintf(selected_root, sizeof(selected_root), "%s", g.fork_root);
        selected.directory = selected_root;
    }
    memset(&g.config, 0, sizeof(g.config));
    g.config = selected;
    if (g.config.rotate_bytes == 0) g.config.rotate_bytes = OPENFTDS_DEFAULT_ROTATE;
    if (g.config.max_files == 0) g.config.max_files = OPENFTDS_DEFAULT_FILES;
    if (g.config.flush_interval_ms == 0) g.config.flush_interval_ms = OPENFTDS_DEFAULT_INTERVAL;
    if (g.config.sample_rate == 0) g.config.sample_rate = 1;

    const char *root = selected.directory ? selected.directory : getenv("OPENFTDS_DIR");
    if (!root || !*root) root = "./openftds";
    if (snprintf(g.root, sizeof(g.root), "%s", root) >= (int)sizeof(g.root) ||
        snprintf(g.stat_dir, sizeof(g.stat_dir), "%s/data/stat", root) >= (int)sizeof(g.stat_dir) ||
        snprintf(g.log_dir, sizeof(g.log_dir), "%s/run/client", root) >= (int)sizeof(g.log_dir)) {
        pthread_mutex_unlock(&g.lifecycle_mu);
        return ENAMETOOLONG;
    }
    int rc = mkdirs(g.stat_dir);
    if (rc == 0) rc = mkdirs(g.log_dir);
    if (rc != 0) {
        pthread_mutex_unlock(&g.lifecycle_mu);
        return rc;
    }

    char hostname[64] = "unknown";
    (void)gethostname(hostname, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = '\0';
    sanitize_component(hostname);
    g.started_epoch = epoch_ns() / 1000000000ull;
    snprintf(g.file_stem, sizeof(g.file_stem), "openftds-%s-%ld-%llu",
             hostname, (long)getpid(), (unsigned long long)g.started_epoch);

    char log_path[PATH_MAX];
    if (snprintf(log_path, sizeof(log_path), "%s/openftds-%ld.log", g.log_dir,
                 (long)getpid()) >= (int)sizeof(log_path)) {
        pthread_mutex_unlock(&g.lifecycle_mu);
        return ENAMETOOLONG;
    }
    g.log = fopen(log_path, "a");
    g.log_fd = g.log ? fileno(g.log) : -1;
    rc = open_output();
    if (rc != 0) {
        if (g.log) fclose(g.log);
        g.log = NULL;
        g.log_fd = -1;
        pthread_mutex_unlock(&g.lifecycle_mu);
        return rc;
    }

    unsigned env_level = env_unsigned("OPENFTDS_MIN_LEVEL", (unsigned)g.config.min_level);
    if (env_level > (unsigned)OPENFTDS_LEVEL_CRITICAL) env_level = OPENFTDS_LEVEL_CRITICAL;
    unsigned runtime_sample_rate = env_unsigned("OPENFTDS_SAMPLE_RATE", g.config.sample_rate);
    if (runtime_sample_rate == 0) runtime_sample_rate = 1;
    int runtime_enabled = inherited ? g.fork_enabled : env_enabled(1);
    atomic_store_explicit(&g.enabled, runtime_enabled, memory_order_release);
    atomic_store_explicit(&g.min_level, (int)env_level, memory_order_release);
    atomic_store_explicit(&g.sample_rate, runtime_sample_rate, memory_order_release);
    g.fork_config_pending = 0;
    g.stopping = 0;
    g.initialized = 1;
    static int atexit_registered = 0;
    if (!atexit_registered) {
        (void)atexit(shutdown_at_exit);
        atexit_registered = 1;
    }
    rc = pthread_create(&g.writer_thread, NULL, writer_main, NULL);
    if (rc == 0) g.writer_started = 1;
    else log_message("background writer unavailable: %s", strerror(rc));
    log_message("collector started root=%s interval_ms=%u rotate_bytes=%zu sample_rate=%u",
                g.root, g.config.flush_interval_ms, g.config.rotate_bytes,
                runtime_sample_rate);
    pthread_mutex_unlock(&g.lifecycle_mu);
    return 0;
}

void openftds_shutdown(void) {
    recover_after_fork_if_needed();
    pthread_mutex_lock(&g.lifecycle_mu);
    if (!g.initialized) {
        pthread_mutex_unlock(&g.lifecycle_mu);
        return;
    }
    g.stopping = 1;
    pthread_cond_broadcast(&g.wake_cv);
    int join_writer = g.writer_started;
    pthread_t writer = g.writer_thread;
    pthread_mutex_unlock(&g.lifecycle_mu);

    if (join_writer && !pthread_equal(pthread_self(), writer)) (void)pthread_join(writer, NULL);
    (void)openftds_flush();

    pthread_mutex_lock(&g.lifecycle_mu);
    log_message("collector stopped");
    if (g.out) fclose(g.out);
    if (g.log) fclose(g.log);
    g.out = NULL;
    g.log = NULL;
    g.out_fd = -1;
    g.log_fd = -1;
    g.writer_started = 0;
    g.initialized = 0;
    pthread_mutex_unlock(&g.lifecycle_mu);
}

static int ensure_initialized(void) {
    recover_after_fork_if_needed();
    pthread_mutex_lock(&g.lifecycle_mu);
    int initialized = g.initialized;
    pthread_mutex_unlock(&g.lifecycle_mu);
    return initialized ? 0 : openftds_init(NULL);
}

static int level_is_enabled(openftds_level_t level) {
    return atomic_load_explicit(&g.enabled, memory_order_relaxed) &&
           (int)level >= atomic_load_explicit(&g.min_level, memory_order_relaxed);
}

static int should_sample(void) {
    unsigned rate = atomic_load_explicit(&g.sample_rate, memory_order_relaxed);
    if (rate <= 1) return 1;
    return sample_counter++ % rate == 0;
}

void openftds_set_enabled(int enabled) {
    if (ensure_initialized() != 0) return;
    atomic_store_explicit(&g.enabled, enabled != 0, memory_order_release);
}

int openftds_is_enabled(void) {
    if (ensure_initialized() != 0) return 0;
    return atomic_load_explicit(&g.enabled, memory_order_acquire) != 0;
}

void openftds_set_min_level(openftds_level_t min_level) {
    if (ensure_initialized() != 0) return;
    if (min_level < OPENFTDS_LEVEL_NORMAL) min_level = OPENFTDS_LEVEL_NORMAL;
    if (min_level > OPENFTDS_LEVEL_CRITICAL) min_level = OPENFTDS_LEVEL_CRITICAL;
    atomic_store_explicit(&g.min_level, (int)min_level, memory_order_release);
}

void openftds_set_sample_rate(unsigned one_in_n) {
    if (ensure_initialized() != 0) return;
    if (one_in_n == 0) one_in_n = 1;
    atomic_store_explicit(&g.sample_rate, one_in_n, memory_order_release);
    sample_counter = 0;
}

int openftds_get_status(openftds_status_t *status) {
    if (!status) return EINVAL;
    recover_after_fork_if_needed();
    memset(status, 0, sizeof(*status));
    status->pid = (long)getpid();
    status->enabled = atomic_load_explicit(&g.enabled, memory_order_acquire) != 0;
    status->min_level = (openftds_level_t)atomic_load_explicit(
        &g.min_level, memory_order_acquire);
    status->sample_rate = atomic_load_explicit(&g.sample_rate, memory_order_acquire);
    pthread_mutex_lock(&g.lifecycle_mu);
    status->initialized = g.initialized;
    status->slow_threshold_ns = g.initialized ? g.config.slow_threshold_ns : 0;
    pthread_mutex_unlock(&g.lifecycle_mu);
    for (unsigned i = 0; i < OPENFTDS_MAX_EVENTS; ++i) {
        if (atomic_load_explicit(&g.slots[i].state, memory_order_acquire) == 2)
            ++status->registered_events;
    }
    return 0;
}

openftds_token_t openftds_delay_start(openftds_level_t level) {
    openftds_token_t token = {0};
    if (ensure_initialized() != 0) return token;
    if (!level_is_enabled(level) || !should_sample()) return token;
    token.start_ns = openftds_now_ns();
    token.pid = (long)getpid();
    return token;
}

static void record_sampled(const char *name, const char *module,
                           uint32_t event_id, openftds_level_t level,
                           uint64_t duration_ns, int status) {
    event_slot_t *slot = find_slot(event_id, name, module, level);
    if (!slot) return;
    unsigned bucket = histogram_bucket(duration_ns);
    lock_slot(slot);
    ++slot->count;
    if (status != 0) ++slot->errors;
    if (g.config.slow_threshold_ns != 0 &&
        duration_ns >= g.config.slow_threshold_ns) ++slot->slow;
    slot->total_ns += duration_ns;
    if (duration_ns < slot->min_ns) slot->min_ns = duration_ns;
    if (duration_ns > slot->max_ns) slot->max_ns = duration_ns;
    ++slot->hist[bucket];
    unlock_slot(slot);
}

void openftds_delay_end(const char *name, const char *module, uint32_t event_id,
                        openftds_level_t level, openftds_token_t token, int status) {
    recover_after_fork_if_needed();
    if (token.start_ns == 0 || token.pid != (long)getpid()) return;
    uint64_t now = openftds_now_ns();
    uint64_t duration = now >= token.start_ns ? now - token.start_ns : 0;
    record_sampled(name, module, event_id, level, duration, status);
}

void openftds_record(const char *name, const char *module, uint32_t event_id,
                     openftds_level_t level, uint64_t duration_ns, int status) {
    if (ensure_initialized() != 0 || !level_is_enabled(level) || !should_sample()) return;
    record_sampled(name, module, event_id, level, duration_ns, status);
}

void openftds_compat_delay_start(uint32_t event_id, openftds_level_t level,
                                 void *token) {
    openftds_token_t started = openftds_delay_start(level);
    if (token) memcpy(token, &started.start_ns, sizeof(started.start_ns));
    if (compat_depth == OPENFTDS_COMPAT_STACK) {
        memmove(&compat_frames[0], &compat_frames[1],
                sizeof(compat_frames[0]) * (OPENFTDS_COMPAT_STACK - 1));
        --compat_depth;
    }
    compat_frames[compat_depth++] =
        (compat_frame_t){event_id, level, started, token != NULL};
}

void openftds_compat_delay_end(const char *name, const char *module,
                               uint32_t event_id, openftds_level_t level,
                               const void *value, int status) {
    recover_after_fork_if_needed();
    for (unsigned i = compat_depth; i > 0; --i) {
        unsigned index = i - 1;
        if (compat_frames[index].event_id != event_id) continue;
        compat_frame_t frame = compat_frames[index];
        if (index + 1 < compat_depth) {
            memmove(&compat_frames[index], &compat_frames[index + 1],
                    sizeof(compat_frames[0]) * (compat_depth - index - 1));
        }
        --compat_depth;
        /* Public FTDS call sites pass a precomputed nanosecond duration at END;
         * our convenience form passes the token written by START. */
        uint64_t supplied = 0;
        if (value) memcpy(&supplied, value, sizeof(supplied));
        if (frame.token.start_ns == 0) return;
        if (value && !frame.caller_token) {
            record_sampled(name, module, event_id, level, supplied, status);
        } else {
            openftds_delay_end(name, module, event_id, level,
                               frame.token, status);
        }
        return;
    }
    if (value) {
        uint64_t supplied;
        memcpy(&supplied, value, sizeof(supplied));
        openftds_record(name, module, event_id, level, supplied, status);
    }
}
