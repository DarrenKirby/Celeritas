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


logger_t logger;


char* l_priority(const int priority)
{
    switch (priority) {
        case L_DEBUG: return "[debug]";
        case L_INFO:  return "[info]";
        case L_WARN:  return "[warn]";
        case L_ERROR: return "[error]";
        default: return "";
    }
}


/* Returns a string with the current time
 * formatted as "2026-04-22 15:36:22 PDT". */
char* l_format_datetime(void)
{
    static char t_buf[24];
    const time_t now = time(nullptr);
    const struct tm *tm = localtime(&now);
    strftime(t_buf, sizeof(t_buf), "%F %T %Z", tm);
    return t_buf;
}


void *get_tid(void)
{
    void* tid = pthread_self();
    return tid;
}


void l_debug(logger_t* log, char* s)
{
    log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - %s\n",
        l_priority(L_DEBUG), l_format_datetime(), conf_data.server_pid, get_tid(), s);
}


void l_info(logger_t* log, char* s)
{
    log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - %s\n",
        l_priority(L_INFO), l_format_datetime(), conf_data.server_pid, get_tid(), s);
}


void l_warn(logger_t* log, char* s)
{
    log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - %s\n",
        l_priority(L_WARN), l_format_datetime(), conf_data.server_pid, get_tid(), s);
}


void l_error(logger_t* log, char* s)
{
    log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - %s\n",
        l_priority(L_ERROR), l_format_datetime(), conf_data.server_pid, get_tid(), s);
}


void log_access(request_ctx_t* ctx, uint64_t latency)
{
    log_write(&ctx->log->ring, LOG_TARGET_ACCESS, "%s - - [%s]  \"%s %s %s\" %d %d latency: %lld\n",
    ctx->conn->remote_ip, l_format_datetime(), ctx->request.h1.method, ctx->request.h1.uri, ctx->request.h1.version,
    ctx->status_code, ctx->response.body_len, latency);
}


void log_write(log_ring_t *ring, const log_target_t target, const char *fmt, ...)
{
    log_entry_t entry;

    /* Format entirely before taking the lock */
    va_list args;
    va_start(args, fmt);
    entry.len = vsnprintf(entry.line, LOG_LINE_MAX, fmt, args);
    entry.target = target;
    va_end(args);

    /* Now touch shared state */
    THR_OK(pthread_mutex_lock(&ring->mutex));

    if (ring->count == LOG_RING_SIZE) {
        /*
         * Ring is full. Two options:
         *   a) Drop the entry (common for access logs under load)
         *   b) Block until space is available
         */
        THR_OK(pthread_mutex_unlock(&ring->mutex));
        return;  /* Dropped.  */
    }

    entry.target = target;
    ring->entries[ring->tail] = entry;
    ring->tail = (ring->tail + 1) & (LOG_RING_SIZE - 1);  /* Cheap % for power-of-2. */
    ring->count++;

    THR_OK(pthread_cond_signal(&ring->not_empty));
    THR_OK(pthread_mutex_unlock(&ring->mutex));
}


void *logger_thread(void *arg)
{

    logger_t   *log  = arg;
    log_ring_t *ring = &log->ring;

    while (1) {
        THR_OK(pthread_mutex_lock(&ring->mutex));

        /* Sleep until there is something to write */
        while (ring->count == 0 && !log->shutdown)
            THR_OK(pthread_cond_wait(&ring->not_empty, &ring->mutex));

        if (ring->count == 0 && log->shutdown) {
            THR_OK(pthread_mutex_unlock(&ring->mutex));
            break;
        }

        /* Dequeue */
        const log_entry_t entry = ring->entries[ring->head];
        ring->head  = (ring->head + 1) & (LOG_RING_SIZE - 1);
        ring->count--;

        THR_OK(pthread_mutex_unlock(&ring->mutex));

        /* Write — outside the lock, only this thread touches the fds */
        const int fd = (entry.target == LOG_TARGET_ACCESS) ? log->access_fd
                                                           : log->event_fd;
        write(fd, entry.line, entry.len);
    }
    return NULL;
}


static void early_fatal(const char *msg)
{
    const int fd = open("/Users/darrenkirby/code/celeritas/logs/startup_fail.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        write(fd, "\n", 1);
        close(fd);
    }
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
    logger.ring.count = 0;
    pthread_mutex_init(&logger.ring.mutex, nullptr);
    pthread_cond_init(&logger.ring.not_empty, nullptr);
    pthread_cond_init(&logger.ring.not_full, nullptr);

    if (pthread_create(&logger.thread, nullptr, logger_thread, &logger) != 0) {
        early_fatal("creating logger thread failed\n");
    }
}


void logger_shutdown(logger_t *log)
{
    /* Signal the logger thread to wake up and exit. */
    pthread_mutex_lock(&log->ring.mutex);
    log->shutdown = 1;
    pthread_cond_signal(&log->ring.not_empty);
    pthread_mutex_unlock(&log->ring.mutex);

    /* Wait for it to finish draining the ring and exit. */
    pthread_join(log->thread, nullptr);

    close(log->access_fd);
    close(log->event_fd);
}
