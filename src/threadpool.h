/*
 * 'src/threadpool.h'
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


#ifndef CELERITAS_THREADPOOL_H
#define CELERITAS_THREADPOOL_H

#include "logger.h"

#include <pthread.h>


#define THR_OK(func) \
    do { \
        int rc = (func); \
        if (rc != 0) { \
            FILE* fh = fopen(conf_data->event_log, "a"); \
            fprintf(fh, "%s:%d: %s failed with error %d\n", __FILE__, __LINE__, #func, rc); \
            abort(); \
        } \
    } while (0)


/* The work queue. */
typedef struct {
    conn_t *buf;
    int capacity;

    int head;   /* Next item to pop */
    int tail;   /* Next slot to push. */
    int count;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;

    int shutting_down;
} work_queue_t;


/* Argument struct for worker_thread. */
typedef struct {
    work_queue_t *queue;
    logger_t *logger;
} worker_args_t;


/* Argument struct for listener_thread. */
typedef struct {
    work_queue_t *queue;
    uint16_t port;
    logger_t *logger;
    uint8_t is_tls;
} listener_args_t;


/* This struct holds our worker threads and socket descriptors
 * so we can access them later for a clean shutdown. */
struct server_t {
    pthread_t *workers;
    int n_workers;

    pthread_t http_listener;
    pthread_t https_listener;

    int http_fd;
    int https_fd;

    work_queue_t *queue;
};


void worker_init(logger_t* log);

#endif //CELERITAS_THREADPOOL_H
