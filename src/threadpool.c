/*
 * 'src/threadpool.c'
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

#include "threadpool.h"
#include "logger.h"
#include "server.h"
#include "socket.h"
#include "util.h"
#include "http_common.h"
#include "validator.h"
#include "handler_static.h"
#include "io_watcher.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>

#define UPGRADE_REQUIRED 1
#define MAX_WAIT_EVENTS 64
#define MAX_TICK_MS 200

server_t server = {0};


/* Initializes the work and wait queues. */
static int queue_init(work_queue_t *q, const int capacity)
{
    q->buf = malloc(sizeof(conn_t) * capacity);
    if (q->buf == NULL) {
        return 1;
    }
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;

    THR_OK(pthread_mutex_init(&q->mutex, nullptr));
    THR_OK(pthread_cond_init(&q->not_empty, nullptr));
    THR_OK(pthread_cond_init(&q->not_full, nullptr));

    q->shutting_down = 0;
    return 0;
}


/* Pushes a connection to the work and wait queues. */
static void queue_push(work_queue_t *q, const conn_t *c)
{
    THR_OK(pthread_mutex_lock(&q->mutex));

    while (q->count == q->capacity) {
        THR_OK(pthread_cond_wait(&q->not_full, &q->mutex));
    }

    q->buf[q->tail] = *c;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    THR_OK(pthread_cond_signal(&q->not_empty));
    THR_OK(pthread_mutex_unlock(&q->mutex));
}


/* Blocking pop connection from the work and wait queues. */
static int queue_pop(work_queue_t *q, conn_t *out)
{
    THR_OK(pthread_mutex_lock(&q->mutex));

    /* Block and wait whilst the queue is empty. */
    while (q->count == 0 && !q->shutting_down) {
        THR_OK(pthread_cond_wait(&q->not_empty, &q->mutex));
    }

    /* Clear the queue before shutting down. */
    if (q->shutting_down && q->count == 0) {
        THR_OK(pthread_mutex_unlock(&q->mutex));
        return -1;
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    /* Signal anyone waiting to push that space is available. */
    THR_OK(pthread_cond_signal(&q->not_full));
    THR_OK(pthread_mutex_unlock(&q->mutex));
    return 0;
}


/* Non-blocking pop connection from the work and wait queues. */
static int queue_trypop(work_queue_t *q, conn_t *out)
{
    THR_OK(pthread_mutex_lock(&q->mutex));

    /* If the queue is empty, return immediately. */
    if (q->count == 0) {
        /* If shutting down, return -1. Otherwise, return 1 to indicate empty. */
        const int ret = q->shutting_down ? -1 : 1;
        THR_OK(pthread_mutex_unlock(&q->mutex));
        return ret;
    }

    /* Since count > 0, grab the item. */
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    /* Signal anyone waiting to push that space is available. */
    THR_OK(pthread_cond_signal(&q->not_full));
    THR_OK(pthread_mutex_unlock(&q->mutex));

    return 0;
}


// ReSharper disable once CppParameterMayBeConstPtrOrRef
void *wait_room_thread(void *arg)
{
    const worker_args_t *wr = arg;
    logger_t *log = wr->logger;
    work_queue_t *work_q = wr->work_queue;
    work_queue_t *wait_q = wr->wait_queue;

    io_watcher_t *watcher = io_watcher_create(log);
    io_event_t events[MAX_WAIT_EVENTS];

    THR_OK(pthread_rwlock_rdlock(&config_lock));
    const uint16_t pool_size = conf_data->conn_queue_size;
    THR_OK(pthread_rwlock_unlock(&config_lock));

    /* Initialize tracking array. */
    conn_pool_t pool;
    pool_init(&pool, pool_size);
    l_debug(log, "initialized keep alive pool at %d", pool_size);

    while (!shutting_down) {
        /* Drain the wait queue, registering each new conn. */
        conn_t conn;
        while (queue_trypop(wait_q, &conn) != 1) {

            /* Add the connection to the tracked array. */
            if (pool_add(&pool, conn) == 0) {
                /* Pass the FD as the userdata. */
                void *safe_userdata = (void*)(intptr_t)conn.fd;

                if (io_watcher_add(watcher, conn.fd, safe_userdata) != 0) {
                    l_error(log, "io_watcher_add failed: %s", strerror(errno));
                    /* Cleanup from pool if kernel registration fails. */
                    pool_remove_by_fd(&pool, conn.fd, nullptr);
                    close_connection(&conn);
                }
            } else {
                l_error(log, "Wait room pool is full!");
                close_connection(&conn);
            }
        }

        /* Compute timeout: time until the nearest expiry. */
        int timeout_ms = pool_nearest_timeout_ms(&pool);

        /* If the pool is empty (-1) or the nearest expiry is far away,
         * cap the wait time so we can check the queue and shutdown flag. */
        if (timeout_ms == -1 || timeout_ms > MAX_TICK_MS) {
            timeout_ms = MAX_TICK_MS;
        }

        /* Wait for readability events. */
        const int n = io_watcher_wait(watcher, events, MAX_WAIT_EVENTS, timeout_ms);
        if (n < 0 && errno != EINTR) {
            l_error(log, "io_watcher_wait: %s", strerror(errno));
        }

        /* Push readable connections back to work queue. */
        for (int i = 0; i < n; i++) {
            /* Retrieve the FD from the userdata. */
            const int ready_fd = (int)(intptr_t)events[i].userdata;

            conn_t active_conn;
            /* Find the associated connection, copy it to active_conn, and remove it from array. */
            if (pool_remove_by_fd(&pool, ready_fd, &active_conn)) {

                if (events[i].is_error) {
                    close_connection(&active_conn);
                } else {
                    queue_push(work_q, &active_conn);
                }
            }
        }

        /* Scan for timed-out connections and close them. */
        pool_sweep_expired(&pool, watcher);
    }
    io_watcher_destroy(watcher);
    free(pool.items);
    return NULL;
}


// ReSharper disable once CppParameterMayBeConstPtrOrRef
void *worker_thread(void *arg) {
    const worker_args_t *wa = arg;
    conn_t conn;

    THR_OK(pthread_rwlock_rdlock(&config_lock));
    const uint16_t timeout = conf_data->keepalive_timeout;
    THR_OK(pthread_rwlock_unlock(&config_lock));

    while (queue_pop(wa->work_queue, &conn) == 0) {
        /* Determine the HTTP version of the request if unknown.
         * This will already be set for keep alive connections. */
        if (conn.protocol == PROTO_UNKNOWN) {
            demux_protocol(&conn);
        }

        request_ctx_t *ctx = calloc(1, sizeof(request_ctx_t));
        ctx->conn = &conn;
        ctx->log = wa->logger;
        ctx->start_time = get_now_us();

        /* Perform TLS handshake. */
        /* FIXME: should this be repeated for keep alive connections? */
        if (conn.is_tls && conn.ssl) {
            const int result = perform_tls_handshake(&conn);
            if (result != 0) {
                /* Not much to do but log the error and drop the connection. */
                l_error(ctx->log, "perform_tls_handshake failed");
                close_connection(&conn);
                continue;
            }
        }

        /* Read and parse request. */
        if (process_ingress(ctx) != 0) {
            /* If parsing headers failed, try to send error response. */
            if (ctx->status_code > 0) {
                send_response(ctx);
                log_access(ctx);
            /* No error code: client closed the connection. */
            } else {
                close_connection(&conn);
            }
            goto request_finish;
        }

        /* Validate and route request. */
        validate_request(ctx);  /* If validation fails, it sets 4xx status inside the function. */
        route_request(ctx);     /* Set ctx->handler. */
        ctx->handler(ctx);      /* Execute the handler (Static/Dynamic/Error). */

        /* Send response and log access. */
        send_response(ctx);
        log_access(ctx);

        /* Persistence check. */
        if (!should_keep_alive(ctx) || shutting_down) {
            close_connection(&conn);
            if (shutting_down) {
                break;
            }
        } else {
            /* Set the socket timeout for the next request. */
            conn.keepalive_deadline = time(nullptr) + timeout;
            queue_push(wa->wait_queue, &conn);
        }

        request_finish:
            //debug_print_request(ctx);
            //debug_print_response(ctx);
            /* Run munmap(), or free heap allocated buffer. */
            cleanup_request_resources(ctx);
            free(ctx);
    }
    return NULL;
}


// ReSharper disable once CppParameterMayBeConstPtrOrRef
void *listener_thread(void *arg)
{
    const listener_args_t *la = arg;
    work_queue_t *q = la->queue;
    logger_t *log = la->logger;
    const int listen_fd = la->sock;

    while (!shutting_down) {
        conn_t conn = {0};
        const int rv = accept_connection(log, listen_fd, la->is_tls, &conn);

        /* The shutdown signal will likely arrive whilst blocking on accept().
         * Ignore whatever accept_connection returned. */
        if (shutting_down) {
            if (conn.fd > 0) close(conn.fd);
            break;
        }

        /* Sleep and try again. */
        if (rv != 0) {
            usleep(10000); /* 10ms — prevents CPU spin. */
            continue;
        }

        /* A proper connection from accept(); push to queue. */
        queue_push(q, &conn);
    }
    return nullptr;
}

/* Initializes the worker thread pool, two listener threads, and a singleton
 * wait room thread. Also initializes the queue infrastructure needed by the threads. */
void worker_init(logger_t* log, const char lockfile[], const int http_sock, const int https_sock)
{
    server.http_fd = http_sock;
    server.https_fd = https_sock;

    /* Get values from config. */
    THR_OK(pthread_rwlock_rdlock(&config_lock));
    const uint16_t cap = conf_data->conn_queue_size;
    const uint16_t N = conf_data->worker_threads;
    THR_OK(pthread_rwlock_unlock(&config_lock));

    /* Initialize the work queue. */
    l_debug(log, "initializing work queue to size %d", cap);
    work_queue_t *work_queue = malloc(sizeof(work_queue_t));
    if (!work_queue) {
        l_error(log, "malloc failed for work queue");
        server_shutdown(log, 1);
    }

    work_queue->buf = nullptr;

    if (queue_init(work_queue, cap) != 0) {
        l_error(log, "queue_init failed for work queue");
    }

    /* Initialize the wait room queue. */
    l_debug(log, "initializing wait room queue to size %d", cap);
    work_queue_t *wait_queue = malloc(sizeof(work_queue_t));
    if (!wait_queue) {
        l_error(log, "malloc failed for wait queue");
        server_shutdown(log, 1);
    }

    wait_queue->buf = nullptr;

    if (queue_init(wait_queue, cap) != 0) {
        l_error(log, "queue_init failed for wait queue");
    }

    server.work_queue = work_queue;
    server.wait_queue = wait_queue;
    strncpy(server.lock_file, lockfile, PATH_MAX);

    /* Set up args for the wait room thread. */
    static worker_args_t wr_args;
    wr_args.logger = log;
    wr_args.work_queue = work_queue;
    wr_args.wait_queue = wait_queue;

    /* Spawn the wait room thread. */
    l_debug(log, "initializing wait room thread");
    pthread_t wr_thread;
    pthread_create(&wr_thread, nullptr, wait_room_thread, &wr_args);
    server.wait_room_thread = wr_thread;

    /* Set up the args for the worker threads. */
    static worker_args_t w_args;
    w_args.logger = log;
    w_args.work_queue = work_queue;
    w_args.wait_queue = wait_queue;

    /* Spawn the workers... */
    l_debug(log, "spawning %d worker threads", N);
    server.n_workers = N;
    server.workers = malloc(sizeof(pthread_t) * N);

    for (int i = 0; i < N; i++) {
        pthread_create(&server.workers[i], nullptr, worker_thread, &w_args);
    }

    /* Set up the args for the http listener thread. */
    static listener_args_t http_l_args;
    http_l_args.logger = log;
    http_l_args.queue = work_queue;
    http_l_args.sock = http_sock;
    http_l_args.is_tls = false;

    /* Spawn the http listener. */
    l_debug(log, "initializing http listener thread");
    pthread_t http_listener;
    pthread_create(&http_listener, nullptr, listener_thread, &http_l_args);
    server.http_listener = http_listener;

    /* Set up the args for the https listener thread. */
    static listener_args_t https_l_args;
    https_l_args.logger = log;
    https_l_args.queue = work_queue;
    https_l_args.sock = https_sock;
    https_l_args.is_tls = true;

    /* Spawn the http listener. */
    l_debug(log, "initializing https listener thread");
    pthread_t https_listener;
    pthread_create(&https_listener, nullptr, listener_thread, &https_l_args);
    server.https_listener = https_listener;
}
