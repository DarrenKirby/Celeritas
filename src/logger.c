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
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <libgen.h>


char* l_priority(const int priority)
{
    switch (priority) {
        case L_DEBUG: return "[debug] ";
        case L_INFO:  return "[info] ";
        case L_WARN:  return "[warn] ";
        case L_ERROR: return "[error] ";
        default: return "";
    }
}


char* l_format_datetime(void)
{
    char *t_buf = nullptr;
    const time_t now = time(NULL);
    const struct tm *tm = localtime(&now);
    strftime(t_buf, sizeof(t_buf), "%Y-%m-%d %H:%M:%S", tm);
    return t_buf;
}


void log_write(log_ring_t *ring, const log_target_t target, const char *fmt, ...)
{
    log_entry_t entry;

    /* --- Format entirely before taking the lock --- */
    va_list args;
    va_start(args, fmt);
    entry.len    = vsnprintf(entry.line, LOG_LINE_MAX, fmt, args);
    entry.target = target;
    va_end(args);

    /* --- Now touch shared state --- */
    pthread_mutex_lock(&ring->mutex);

    if (ring->count == LOG_RING_SIZE) {
        /*
         * Ring is full. Two options:
         *   a) Drop the entry (common for access logs under load)
         *   b) Block until space is available
         *
         * For an error log you might prefer (b) so nothing is lost.
         * For an access log under heavy traffic (a) is more appropriate
         * so worker threads never stall waiting for the logger.
         */
        pthread_mutex_unlock(&ring->mutex);
        return;  /* dropped — optionally increment a counter */
    }

    entry.target = target;
    ring->entries[ring->tail] = entry;
    ring->tail = (ring->tail + 1) & (LOG_RING_SIZE - 1);  /* cheap % for power-of-2 */
    ring->count++;

    pthread_cond_signal(&ring->not_empty);
    pthread_mutex_unlock(&ring->mutex);
}


void *logger_thread(void *arg)
{
    logger_t   *log  = arg;
    log_ring_t *ring = &log->ring;

    while (1) {
        pthread_mutex_lock(&ring->mutex);

        /* Sleep until there is something to write */
        while (ring->count == 0 && !log->shutdown)
            pthread_cond_wait(&ring->not_empty, &ring->mutex);

        if (ring->count == 0 && log->shutdown) {
            pthread_mutex_unlock(&ring->mutex);
            break;
        }

        /* Dequeue */
        const log_entry_t entry = ring->entries[ring->head];
        ring->head  = (ring->head + 1) & (LOG_RING_SIZE - 1);
        ring->count--;

        pthread_mutex_unlock(&ring->mutex);

        /* Write — outside the lock, only this thread touches the fds */
        const int fd = (entry.target == LOG_TARGET_ACCESS) ? log->access_fd
                                                      : log->error_fd;
        write(fd, entry.line, entry.len);
    }

    return NULL;
}


logger_t logger_init(void)
{
    /* Initialize logger_t. */
    logger_t log;

    /* We still have our stdin, stdout, and stderr descriptors at this point */

    /* Ensure path to logs exists. */
    if (access(dirname(conf_data.access_log), F_OK) != 0) {
        if (mkdir(dirname(conf_data.access_log), 0755) != 0) {
            fprintf(stderr, "Error creating directory: %s\n", strerror(errno));
            EXIT_FAILURE;
        }
    }
    if (access(dirname(conf_data.event_log), F_OK) != 0) {
        if (mkdir(dirname(conf_data.event_log), 0755) != 0) {
            fprintf(stderr, "Error creating directory: %s\n", strerror(errno));
            EXIT_FAILURE;
        }
    }

    /* Open access log. */
    log.access_fd = open(conf_data.access_log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log.access_fd < 0) {
        fprintf(stderr, "Opening access log for writing failed: %s\n", strerror(errno));
        EXIT_FAILURE;
    }

    /* Open error log. */
    log.error_fd  = open(conf_data.event_log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log.error_fd < 0) {
        fprintf(stderr, "Opening error log for writing failed: %s\n", strerror(errno));
        EXIT_FAILURE;
    }

    /* Set shutdown flag. */
    log.shutdown = 0;

    /* Initialize log ring buffer. */
    log_ring_t buffer;
    buffer.head = 0;
    buffer.tail = 0;
    buffer.count = 0;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    buffer.mutex = mtx;
    log.ring = buffer;

    /* Initialize logger thread. */
    pthread_t thread;
    if (pthread_create(&thread, NULL, logger_thread, NULL) != 0) {
        fprintf(stderr, "Error creating thread: %s\n", strerror(errno));
        EXIT_FAILURE;
    }

    log.thread = thread;
    return log;
}


void logger_shutdown(logger_t *log)
{
    /* Signal the logger thread to wake up and exit */
    pthread_mutex_lock(&log->ring.mutex);
    log->shutdown = 1;
    pthread_cond_signal(&log->ring.not_empty);
    pthread_mutex_unlock(&log->ring.mutex);

    /* Wait for it to finish draining the ring and exit */
    pthread_join(log->thread, NULL);

    close(log->access_fd);
    close(log->error_fd);
}
