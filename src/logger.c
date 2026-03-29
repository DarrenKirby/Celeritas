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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <libgen.h>
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


void log_access(request_ctx_t* ctx, const uint64_t latency)
{
    log_write(ctx->log, LOG_TARGET_ACCESS, "%s - - [%s]  \"%s %s %s\" %d %d [%lluus] - %s - tid: 0x%lx\n",
    ctx->conn->remote_ip, l_format_datetime(), ctx->request.h1.method, ctx->request.h1.uri, ctx->request.h1.version,
    ctx->status_code, ctx->response.body_len, (unsigned long long)latency,
    confirm_header_exists(ctx, "User-Agent") ? get_header_value(ctx, "User-Agent") : "", get_tid());
}


void log_write(logger_t* log, const log_target_t target, const char *fmt, ...)
{
    log_entry_t entry;

    /* Format entirely before taking the lock. */
    va_list args;
    va_start(args, fmt);
    entry.len = vsnprintf(entry.line, LOG_LINE_MAX, fmt, args);
    entry.target = target;
    va_end(args);

    /* Now touch shared state. */
    THR_OK(pthread_mutex_lock(&log->ring.mutex));

    /* Pre-calculate where the tail will go next. */
    const int next_tail = (log->ring.tail + 1) & (LOG_RING_SIZE - 1);

    /* Check if full. */
    if (next_tail == log->ring.head) {
        if (target == LOG_TARGET_ACCESS) {
            /* Drop access logs under heavy load. */
            THR_OK(pthread_mutex_unlock(&log->ring.mutex));
            return;
        }
        /* Block event/error logs until the logger frees up space. */
        while (next_tail == log->ring.head && !log->shutdown) {
            THR_OK(pthread_cond_wait(&log->ring.not_full, &log->ring.mutex));
        }

        /* If the thread woke up because of a shutdown, bail out. */
        if (log->shutdown) {
            THR_OK(pthread_mutex_unlock(&log->ring.mutex));
            return;
        }
    }

    entry.target = target;
    log->ring.entries[log->ring.tail] = entry;
    log->ring.tail = next_tail;

    THR_OK(pthread_cond_signal(&log->ring.not_empty));
    THR_OK(pthread_mutex_unlock(&log->ring.mutex));
}


void *logger_thread(void *arg)
{
    logger_t *log = arg;

    while (1) {
        log_entry_t local_batch[BATCH_SIZE];
        int batch_count = 0;

        /* ### Critical section start. ### */
        THR_OK(pthread_mutex_lock(&log->ring.mutex));

        /* Sleep until there is something to write. */
        while (log->ring.tail == log->ring.head && !log->shutdown) {
            THR_OK(pthread_cond_wait(&log->ring.not_empty, &log->ring.mutex));
        }

        /* If empty and shutting down, bail out */
        if (log->ring.tail == log->ring.head && log->shutdown) {
            THR_OK(pthread_mutex_unlock(&log->ring.mutex));
            break;
        }

        /* Grab up to BATCH_SIZE entries at once. */
        while (log->ring.head != log->ring.tail && batch_count < BATCH_SIZE) {
            local_batch[batch_count++] = log->ring.entries[log->ring.head];
            log->ring.head = (log->ring.head + 1) & (LOG_RING_SIZE - 1);
        }

        /* Wake up ANY workers blocked on a full queue, since we just cleared space. */
        if (batch_count > 0) {
            THR_OK(pthread_cond_broadcast(&log->ring.not_full));
        }

        THR_OK(pthread_mutex_unlock(&log->ring.mutex));
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


static void early_fatal(const char *msg)
{
    const int fd = open("/Users/darrenkirby/code/celeritas/logs/startup_fail.log",
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


void logger_init(void)
{
    /* Ensure path to logs exists. */
    char *access_dir = strdup(conf_data.access_log);
    if (access(dirname(access_dir), F_OK) != 0) {
        free(access_dir);
        access_dir = strdup(conf_data.access_log);
        if (mkdir(dirname(conf_data.access_log), 0755) != 0) {
            early_fatal("mkdir failed\n");
        }
    }
    free(access_dir);
    char *event_dir = strdup(conf_data.event_log);
    if (access(dirname(event_dir), F_OK) != 0) {
        free(event_dir);
        event_dir = strdup(conf_data.event_log);
        if (mkdir(dirname(conf_data.event_log), 0755) != 0) {
            early_fatal("mkdir failed\n");
        }
    }
    free(event_dir);

    /* Assign the log descriptors. */
    logger.access_fd = open(conf_data.access_log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logger.access_fd < 0) {
        early_fatal("opening access_log failed\n");
    }

    logger.event_fd = open(conf_data.event_log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logger.event_fd < 0) {
        early_fatal("opening event_log failed\n");
    }

    logger.shutdown = 0;
    logger.ring.head  = 0;
    logger.ring.tail  = 0;

    pthread_mutex_init(&logger.ring.mutex, nullptr);
    pthread_cond_init(&logger.ring.not_empty, nullptr);
    pthread_cond_init(&logger.ring.not_full, nullptr);

    if (pthread_create(&logger.thread, nullptr, logger_thread, &logger) != 0) {
        early_fatal("creating logger thread failed\n");
    }
}


void logger_shutdown(logger_t *log)
{
    pthread_mutex_lock(&log->ring.mutex);
    log->shutdown = 1;

    /* Wake up the logger thread so it can drain and exit. */
    pthread_cond_signal(&log->ring.not_empty);

    /* Wake up ALL blocked worker threads so they don't hang forever. */
    pthread_cond_broadcast(&log->ring.not_full);

    pthread_mutex_unlock(&log->ring.mutex);

    /* Wait for the logger to finish draining the ring and exit. */
    pthread_join(log->thread, nullptr);

    close(log->access_fd);
    close(log->event_fd);
}
