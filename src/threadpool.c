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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <errno.h>


server_t server = {0};
extern _Atomic int shutting_down;


void handle_connection(conn_t* conn, logger_t* log) {
    l_debug(log, "got a connection");
    write(conn->fd,
      "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello",
      44);
    log_write(&log->ring, LOG_TARGET_ACCESS,
    "%s - - [%s] \"GET / HTTP/1.1\" 200 5\n",
    conn->remote_ip,
    l_format_datetime());
}


int queue_init(work_queue_t *q, const int capacity)
{
    q->buf = malloc(sizeof(conn_t) * capacity);
    if (q->buf == NULL) {
        return 1;
    }
    q->capacity = capacity;
    q->head = q->tail = q->count = 0;

    pthread_mutex_init(&q->mutex, nullptr);
    pthread_cond_init(&q->not_empty, nullptr);
    pthread_cond_init(&q->not_full, nullptr);

    q->shutting_down = 0;
    return 0;
}


void queue_push(work_queue_t *q, const conn_t *c)
{
    pthread_mutex_lock(&q->mutex);

    while (q->count == q->capacity) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }

    q->buf[q->tail] = *c;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}


int queue_pop(work_queue_t *q, conn_t *out)
{
    pthread_mutex_lock(&q->mutex);

    while (q->count == 0 && !q->shutting_down) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }

    if (q->shutting_down && q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }

    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}


void *worker_thread(void *arg)
{
    const worker_args_t *wa = arg;
    logger_t *log = wa->logger;
    work_queue_t *q = wa->queue;
    conn_t conn;

    l_debug(log, "in worker thread");

    for (;;) {
        if (queue_pop(q, &conn) != 0) break;

        handle_connection(&conn, log);

        close(conn.fd);
        if (conn.ssl) {
            SSL_shutdown(conn.ssl);
            SSL_free(conn.ssl);
        }
    }
    return nullptr;
}


static int create_listener(uint16_t port, logger_t *log)
{
    const int fd = socket(AF_INET6, SOCK_STREAM, 0);

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);

    const int ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret != 0) {
        log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - bind failed: %s\n",
                l_priority(L_ERROR), l_format_datetime(), conf_data.server_pid, get_tid(), strerror(errno));
        close(fd);
        return -1;
    }
    listen(fd, 128);

    return fd;
}


void *listener_thread(void *arg)
{
    const listener_args_t *la = arg;
    work_queue_t *q = la->queue;
    logger_t *log = la->logger;

    log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - binding to port %d\n",
        l_priority(L_INFO), l_format_datetime(), conf_data.server_pid, get_tid(), la->port);

    /* Next step... */
    const int listen_fd = create_listener(la->port, log);

    if (la->is_tls) {
        server.https_fd = listen_fd;
    } else {
        server.http_fd = listen_fd;
    }

    for (;;) {
        if (shutting_down) break;

        struct sockaddr_storage addr;
        socklen_t len = sizeof(addr);

        const int fd = accept(listen_fd, (struct sockaddr*)&addr, &len);
        if (fd < 0) {
            if (shutting_down) break;
            continue;
        }

        l_debug(log, "listener got connection");

        conn_t conn = {0};
        conn.fd = fd;
        conn.is_tls = la->is_tls;

        // Fill remote IP + port
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *a = (struct sockaddr_in*)&addr;
            inet_ntop(AF_INET, &a->sin_addr, conn.remote_ip, sizeof(conn.remote_ip));
            conn.remote_port = ntohs(a->sin_port);
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *a = (struct sockaddr_in6*)&addr;
            inet_ntop(AF_INET6, &a->sin6_addr, conn.remote_ip, sizeof(conn.remote_ip));
            conn.remote_port = ntohs(a->sin6_port);
        }

        // TLS later (do in worker ideally)
        conn.ssl = nullptr;

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
    work_queue_t* queue = malloc(sizeof(*queue));
    if (queue == NULL) {
        l_error(log, "malloc failed");
        server_shutdown(log, 1);
    }

    if (queue_init(queue, cap) != 0) {
        l_error(log, "queue_init failed");
    }

    server.queue = queue;

    /* Set up the args for the worker threads. */
    static worker_args_t w_args;
    w_args.logger = log;
    w_args.queue = queue;

    /* Spawn the workers... */
    log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - spawning %d worker threads\n",
        l_priority(L_INFO), l_format_datetime(), conf_data.server_pid, get_tid(), N);
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
