/*
 * 'src/logger.c'
 * This file is part of celeritas - https://github.com/DarrenKirby/celeritas
 * Copyright © 2026 Darren Kirby <darren@dragonbyte.ca>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "logger.h"
#include "threadpool.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "util.h"

#define BATCH_SIZE 32


logger_t logger;


char* l_priority(const int priority)
{
    switch (priority) {
        case L_DEBUG: return "[debug]";
        case L_INFO:  return " [info]";
        case L_WARN:  return " [warn]";
        case L_ERROR: return "[error]";
        default: return "";
    }
}


/* Returns a string with the current time
 * formatted as "2026-04-22 15:36:22 PDT". */
char* l_format_datetime(void)
{
    /* __thread (or _Thread_local in C11) gives every thread its own buffer. */
    static __thread char t_buf[32];

    struct tm tm_info;
    const time_t now = time(nullptr);

    /* localtime_r writes directly into the stack-allocated tm_info. */
    localtime_r(&now, &tm_info);

    strftime(t_buf, sizeof(t_buf), "%F %T %Z", &tm_info);

    return t_buf;
}


unsigned long get_tid(void)
{
    return (unsigned long)pthread_self();
}


void log_access(request_ctx_t* ctx)
{
    const uint64_t latency = get_now_us() - ctx->start_time;
    log_write(ctx->log, LOG_TARGET_ACCESS, "%s - - [%s]  \"%s %s %s\" %d %d [%lluμs] - %s - tid: 0x%lx\n",
    ctx->conn->remote_ip, l_format_datetime(), ctx->request.h1.method, ctx->request.h1.uri, ctx->request.h1.version,
    ctx->status_code, ctx->response.body_len, (unsigned long long)latency,
    confirm_header_exists(ctx, "User-Agent") ? get_header_value(ctx, "User-Agent") : "", get_tid());
}


void log_write(const logger_t* log, const log_target_t target, const char *fmt, ...)
{
    log_entry_t entry;

    /* Format entirely before taking the lock. */
    va_list args;
    va_start(args, fmt);
    entry.len = vsnprintf(entry.line, LOG_LINE_MAX, fmt, args);
    entry.target = target;
    va_end(args);

    /* Now touch shared state. */
    THR_OK(pthread_mutex_lock(&log->ring->mutex));

    /* Pre-calculate where the tail will go next. */
    const int next_tail = (log->ring->tail + 1) & (log->ring_size - 1);

    /* Check if full. */
    if (next_tail == log->ring->head) {
        if (target == LOG_TARGET_ACCESS) {
            /* Drop access logs under heavy load. */
            THR_OK(pthread_mutex_unlock(&log->ring->mutex));
            return;
        }
        /* Block event/error logs until the logger frees up space. */
        while (next_tail == log->ring->head && !log->shutdown) {
            THR_OK(pthread_cond_wait(&log->ring->not_full, &log->ring->mutex));
        }

        /* If the thread woke up because of a shutdown, bail out. */
        if (log->shutdown) {
            THR_OK(pthread_mutex_unlock(&log->ring->mutex));
            return;
        }
    }

    entry.target = target;
    log->ring->entries[log->ring->tail] = entry;
    log->ring->tail = next_tail;

    THR_OK(pthread_cond_signal(&log->ring->not_empty));
    THR_OK(pthread_mutex_unlock(&log->ring->mutex));
}


void *logger_thread(void *arg)
{
    logger_t *log = arg;

    while (1) {
        log_entry_t local_batch[BATCH_SIZE];
        int batch_count = 0;

        /* ### Critical section start. ### */
        THR_OK(pthread_mutex_lock(&log->ring->mutex));

        /* Sleep until there is something to write. */
        while (log->ring->tail == log->ring->head && !log->shutdown) {
            THR_OK(pthread_cond_wait(&log->ring->not_empty, &log->ring->mutex));
        }

        /* If empty and shutting down, bail out */
        if (log->ring->tail == log->ring->head && log->shutdown) {
            THR_OK(pthread_mutex_unlock(&log->ring->mutex));
            break;
        }

        /* Grab up to BATCH_SIZE entries at once. */
        while (log->ring->head != log->ring->tail && batch_count < BATCH_SIZE) {
            local_batch[batch_count++] = log->ring->entries[log->ring->head];
            log->ring->head = (log->ring->head + 1) & (log->ring_size - 1);
        }

        /* Wake up ANY workers blocked on a full queue, since we just cleared space. */
        if (batch_count > 0) {
            THR_OK(pthread_cond_broadcast(&log->ring->not_full));
        }

        THR_OK(pthread_mutex_unlock(&log->ring->mutex));
        /* ### Critical section end. ### */

        /* Set up the iovec array to point to the local copies. */
        struct iovec iov_access[BATCH_SIZE];
        struct iovec iov_event[BATCH_SIZE];
        int access_count = 0, event_count = 0;

        for (int i = 0; i < batch_count; i++) {
            if (local_batch[i].target == LOG_TARGET_ACCESS) {
                iov_access[access_count].iov_base = local_batch[i].line;
                iov_access[access_count].iov_len  = local_batch[i].len;
                access_count++;
            } else {
                iov_event[event_count].iov_base = local_batch[i].line;
                iov_event[event_count].iov_len  = local_batch[i].len;
                event_count++;
            }
        }

        /* Fire them off in a burst. */
        if (access_count > 0) {
            const ssize_t rv = writev(log->access_fd, iov_access, access_count);
            if (rv < 0) {
                l_warn(log, "access log write failed");
                continue;
            }
        }
        if (event_count > 0) {
            const ssize_t rv = writev(log->event_fd, iov_event, event_count);
            if (rv < 0) {
                l_warn(log, "event log write failed");
            }
        }
    }
    return NULL;
}


void early_fatal(const char *msg)
{
    const int fd = open("./startup_fail.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        ssize_t rv = write(fd, msg, strlen(msg));
        if (rv < 0) {
            close(fd);
            exit(1);
        }
        rv = write(fd, "\n", 1);
        if (rv < 0) {
            close(fd);
            exit(1);
        }
    }
    close(fd);
    exit(1);
}


logger_t* logger_init(void)
{
    char alog[PATH_MAX];
    char elog[PATH_MAX];

    /* Grab read lock to read config. */
    THR_OK(pthread_rwlock_rdlock(&config_lock));
    strncpy(alog, conf_data->access_log, PATH_MAX);
    strncpy(elog, conf_data->event_log, PATH_MAX);
    const uint16_t log_buf_size = conf_data->log_queue_size;
    THR_OK(pthread_rwlock_unlock(&config_lock));

    /* Ensure path to logs exists. */
    validate_path(alog);
    validate_path(elog);

    /* Assign the log descriptors. */
    logger.access_fd = open(alog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logger.access_fd < 0) {
        early_fatal("opening access log failed\n");
    }
    logger.event_fd = open(elog, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logger.event_fd < 0) {
        early_fatal("opening event log failed\n");
    }

    logger.shutdown = 0;

    /* Allocate log ring buffer. */
    logger.ring = calloc(1, sizeof(struct log_ring_t));
    if (!logger.ring) {
        early_fatal("failed to allocate log buffer\n");
    }

    /* Allocate array of log ring buffer entries. */
    logger.ring->entries = calloc(1, sizeof(struct log_entry_t) * log_buf_size);
    if (!logger.ring) {
        early_fatal("failed to allocate log buffer\n");
    }

    logger.ring_size = log_buf_size;
    logger.ring->head  = 0;
    logger.ring->tail  = 0;

    THR_OK(pthread_mutex_init(&logger.ring->mutex, nullptr));
    THR_OK(pthread_cond_init(&logger.ring->not_empty, nullptr));
    THR_OK(pthread_cond_init(&logger.ring->not_full, nullptr));

    if (pthread_create(&logger.thread, nullptr, logger_thread, &logger) != 0) {
        early_fatal("creating logger thread failed\n");
    }
    return &logger;
}


void logger_shutdown(logger_t *log)
{
    THR_OK(pthread_mutex_lock(&log->ring->mutex));
    log->shutdown = 1;

    /* Wake up the logger thread so it can drain and exit. */
    THR_OK(pthread_cond_signal(&log->ring->not_empty));

    /* Wake up ALL blocked worker threads so they don't hang forever. */
    THR_OK(pthread_cond_broadcast(&log->ring->not_full));

    THR_OK(pthread_mutex_unlock(&log->ring->mutex));

    /* Wait for the logger to finish draining the ring. */
    THR_OK(pthread_join(log->thread, nullptr));

    /* Deallocate lg buffer and close the files. */
    free(log->ring->entries);
    free(log->ring);

    close(log->access_fd);
    close(log->event_fd);
}
