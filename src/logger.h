/*
 * 'src/logger.h'
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


#ifndef CELERITAS_LOGGER_H
#define CELERITAS_LOGGER_H

#include <pthread.h>

#define LOG_LINE_MAX  2048
#define LOG_RING_SIZE 512   /* must be power of 2 for cheap modulo */


typedef enum {
    L_DEBUG,
    L_INFO,
    L_WARN,
    L_ERROR,
    L_ACCESS
} log_level_t;


typedef enum {
    LOG_TARGET_ACCESS,
    LOG_TARGET_EVENT
} log_target_t;


typedef struct Log_Entry_T {
    log_target_t target;
    int          len;
    char         line[LOG_LINE_MAX];
} log_entry_t;


typedef struct Log_Ring_T {
    log_entry_t     entries[LOG_RING_SIZE];
    int             head;       /* logger thread reads from here  */
    int             tail;       /* producers write to here        */
    int             count;      /* current number of entries      */
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;  /* logger sleeps on this          */
    pthread_cond_t  not_full;   /* producers sleep on this if full */
} log_ring_t;


typedef struct Logger_T {
    int        access_fd; /* access log file descriptor. */
    int        event_fd;  /* error log file descriptor. */
    log_ring_t ring;      /* The log entry ring buffer. */
    pthread_t  thread;    /* The singleton logger thread ID. */
    int        shutdown;  /* set to 1 by main thread to signal exit */
} logger_t;


void l_debug(logger_t* log, char* s);
void l_info(logger_t* log, char* s);
void l_warn(logger_t* log, char* s);
void l_error(logger_t* log, char* s);
void logger_init(void);
void logger_shutdown(logger_t *log);
char* l_format_datetime(void);
char* l_priority(int priority);
void *get_tid(void);
void log_write(log_ring_t *ring, log_target_t target, const char *fmt, ...);


#endif //CELERITAS_LOGGER_H