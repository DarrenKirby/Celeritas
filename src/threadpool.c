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
//#include "tls.h"
#include "http_common.h"
#include "validator.h"
#include "handler_static.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

#define UPGRADE_REQUIRED 1


server_t server = {0};


int queue_init(work_queue_t *q, const int capacity)
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


void queue_push(work_queue_t *q, const conn_t *c)
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


int queue_pop(work_queue_t *q, conn_t *out)
{
    THR_OK(pthread_mutex_lock(&q->mutex));

    while (q->count == 0 && !q->shutting_down) {
        THR_OK(pthread_cond_wait(&q->not_empty, &q->mutex));
    }

    if (q->shutting_down && q->count == 0) {
        THR_OK(pthread_mutex_unlock(&q->mutex));
        return -1;
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    THR_OK(pthread_cond_signal(&q->not_full));
    THR_OK(pthread_mutex_unlock(&q->mutex));
    return 0;
}


void *worker_thread(void *arg) {
    const worker_args_t *wa = arg;
    conn_t conn;

    while (queue_pop(wa->queue, &conn) == 0) {
        bool persistent = true;

        /* Determine the HTTP version of the request.*/
        demux_protocol(&conn);

        /* Keep-alive loop. */
        while (persistent) {
            request_ctx_t *ctx = calloc(1, sizeof(request_ctx_t));
            ctx->conn = &conn;
            ctx->log = wa->logger;
            ctx->start_time = get_now_us();

            /* Perform TLS handshake (NO-OP for now...) */
            // if (conn.is_tls && perform_tls_handshake(ctx) != 0) {
            //     ctx->status_code = 400;
            // }

            /* Read and parse request. */
            if (process_ingress(ctx) != 0) {
                /* If we can't even read headers, we can't keep the connection alive. */
                persistent = false;
                if (ctx->status_code > 0) send_response(ctx);
                goto request_finish;
            }

            /* Validate and route request. */
            validate_request(ctx);  /* If validation fails, it sets 4xx status inside the function. */
            route_request(ctx);     /* Set ctx->handler. */
            ctx->handler(ctx);      /* Execute the handler (Static/Dynamic/Error). */

            /* Send response and log access. */
            send_response(ctx);

            const uint64_t latency = get_now_us() - ctx->start_time;
            log_access(ctx, latency);

            /* Persistence check. */
            if (!should_keep_alive(ctx) || shutting_down) {
                persistent = false;
            } else {
                /* Set the socket timeout for the next request. */
                set_socket_timeout(conn.fd, conf_data.keepalive_timeout);
            }

            request_finish:
                //debug_print_request(ctx);
                //debug_print_response(ctx);
                /* Run munmap(), or free heap allocated buffer. */
                cleanup_request_resources(ctx);
                free(ctx);
        }
        close_connection(&conn);
    }
    return NULL;
}


void *listener_thread(void *arg)
{
    const listener_args_t *la = arg;
    work_queue_t *q = la->queue;
    logger_t *log = la->logger;

    l_info(log, "binding to port: %d", la->port);

    /* Next step... */
    const int listen_fd = create_listener(la->port, log);

    if (la->is_tls) {
        server.https_fd = listen_fd;
    } else {
        server.http_fd = listen_fd;
    }

    while (!shutting_down) {
        conn_t conn = {0};
        const int rv = accept_connection(listen_fd, la->is_tls, &conn);

        /* The shutdown signal will likely arrive whilst blocking on accept().
         * Ignore whatever accept_connection returned. */
        if (shutting_down) {
            if (conn.fd > 0) close(conn.fd);
            break;
        }

        /* Log accept() failure. */
        if (rv != 0) {
            l_warn(log, "accept() failed: %s", strerror(rv));
            continue;
        }

        /* A proper connection from accept(); push to queue. */
        queue_push(q, &conn);
    }
    return nullptr;
}


void worker_init(logger_t* log)
{
    /* Get values from config. */
    const uint16_t cap = conf_data.queue_depth;
    const uint16_t N = conf_data.max_threads;
    const uint16_t http_port = conf_data.http_port;
    const uint16_t https_port = conf_data.https_port;

    /* Initialize the work queue. */
    l_debug(log, "initializing work queue");
    work_queue_t *queue = malloc(sizeof(work_queue_t));
    if (!queue) {
        l_error(log, "malloc failed");
        server_shutdown(log, 1);
    }

    queue->buf = nullptr;

    if (queue_init(queue, cap) != 0) {
        l_error(log, "queue_init failed");
    }

    server.queue = queue;

    /* Set up the args for the worker threads. */
    static worker_args_t w_args;
    w_args.logger = log;
    w_args.queue = queue;

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
    http_l_args.queue = queue;
    http_l_args.port = http_port;
    http_l_args.is_tls = false;

    /* Spawn the http listener. */
    l_debug(log, "initializing http listener thread");
    pthread_t http_listener;
    pthread_create(&http_listener, nullptr, listener_thread, &http_l_args);
    server.http_listener = http_listener;

    /* Set up the args for the https listener thread. */
    static listener_args_t https_l_args;
    https_l_args.logger = log;
    https_l_args.queue = queue;
    https_l_args.port = https_port;
    https_l_args.is_tls = true;

    /* Spawn the http listener. */
    l_debug(log, "initializing https listener thread");
    pthread_t https_listener;
    pthread_create(&https_listener, nullptr, listener_thread, &https_l_args);
    server.https_listener = https_listener;
}
