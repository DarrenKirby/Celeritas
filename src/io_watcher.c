/*
 * 'src/io_watcher.c'
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

#include "io_watcher.h"
#include "logger.h"
#include "server.h"
#include "socket.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>


#ifndef __linux__
  #include <sys/event.h>
#else
  #include <sys/epoll.h>
#endif


/* Initializes and returns the kqueue/epoll file descriptor. */
io_watcher_t *io_watcher_create(logger_t *log)
{
    io_watcher_t *w = malloc(sizeof(io_watcher_t));
    if (w == NULL) {
        l_error(log, "failed to allocate io_watcher_t");
        server_shutdown(log, 1);
    }
#ifndef __linux__
    const int fd = kqueue();
    /* Not sure if this error is recoverable. */
    if (fd < 0) {
        l_error(log, "error creating kqueue: %s", strerror(errno));
        server_shutdown(log, 1);
    }
#else
    const int fd = epoll_create1(0);
    if (fd < 0) {
        l_error(log, "error creating epoll: %s", strerror(errno));
        server_shutdown(log, 1);
    }
#endif
    w->fd = fd;
    return w;
}


/* Closes the I/O watcher FD and frees the memory. */
void io_watcher_destroy(io_watcher_t *w)
{
    close(w->fd);
    free(w);
}


/* Registers a connection with the kernel poll mechanism.
 * When kqueue fires an event, it deregisters the FD automatically.
 * epoll() on Linux does not, it only 'disarms' it. For performance,
 * we allow it to stay, and re-arm it if the connection comes around
 * again. It will ultimately be removed when the connection FD is closed. */
int io_watcher_add(const io_watcher_t *w, const int fd, void *userdata) {
#ifndef __linux__
    struct kevent event;
    /* Initialize kevent structure. */
    EV_SET(&event, fd, EVFILT_READ, EV_ADD|EV_ONESHOT, 0, 0, userdata);
    /* Send the event to kqueue. */
    if (kevent(w->fd, &event, 1, NULL, 0, NULL) != 0) {
        return -1;
    }
#else
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.ptr = userdata;

    if (epoll_ctl(w->fd, EPOLL_CTL_ADD, fd, &event) != 0) {
        if (errno == EEXIST) {
            /* It's already in the tree from a previous keep-alive cycle.
             * Just re-arm it. */
            if (epoll_ctl(w->fd, EPOLL_CTL_MOD, fd, &event) == 0) {
                return 0;
            }
        }
        return -1;
    }
#endif
    return 0;
}


/* Remove a connection from the kernel polling mechanism.
 * This is used when the connection times out prior to firing. */
int io_watcher_remove(const io_watcher_t *w, const int fd)
{
#ifndef __linux__
    struct kevent event;
    EV_SET(&event, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    if (kevent(w->fd, &event, 1, NULL, 0, NULL) != 0) {
        return -1;
    }
#else
    if (epoll_ctl(w->fd, EPOLL_CTL_DEL, fd, NULL) != 0) {
        return -1;
    }
#endif
    return 0;
}


/* Wait for connections to become read-ready. */
int io_watcher_wait(const io_watcher_t *w, io_event_t *events, const int max_events, const int timeout_ms)
{
    int num_events;
#ifndef __linux__
    struct kevent ev_list[max_events];
    struct timespec timeout;

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;

    num_events = kevent(w->fd, NULL, 0, ev_list, max_events, &timeout);

    if (num_events < 0) {
        return -1;
    }

    for (int i = 0; i < num_events; i++) {
        if (ev_list[i].flags & EV_ERROR) {
            events[i].is_error = 1;
            events[i].userdata = ev_list[i].udata;
        } else {
            events[i].is_error = 0;
            events[i].userdata = ev_list[i].udata;
        }
    }
#else
    struct epoll_event ep_list[max_events];
    num_events = epoll_wait(w->fd, ep_list, max_events, timeout_ms);
    if (num_events < 0) {
        return -1;
    }

    for (int i = 0; i < num_events; i++) {
        events[i].userdata = ep_list[i].data.ptr;
        events[i].is_error = (ep_list[i].events & (EPOLLERR | EPOLLHUP)) != 0;
    }
#endif
    return num_events;
}


/* As there is no clean way to get a list of which
 * FDs are being monitored from the kernel itself,
 * we must keep track of that ourselves, externally. */

/* Initialize the connection pool. */
int pool_init(conn_pool_t *pool, const size_t max_conns)
{
    pool->items = malloc(max_conns * sizeof(conn_t));
    if (!pool->items) return -1;

    pool->count = 0;
    pool->capacity = max_conns;
    return 0;
}


/* Add connections to the connection pool. */
int pool_add(conn_pool_t *pool, const conn_t new_conn)
{
    if (pool->count >= pool->capacity) {
        return -1; /* Pool is full. */
    }

    pool->items[pool->count] = new_conn;
    pool->count++;
    return 0;
}


/* Removes connections from the connection pool. */
void pool_remove_at(conn_pool_t *pool, const size_t index)
{
    if (index >= pool->count) return;

    /* Replace the removed item with the last item in the array. */
    pool->items[index] = pool->items[pool->count - 1];

    /* Shrink the active size. */
    pool->count--;
}


/* Calculates the exact ms until the next connection expires.
 * Returns -1 if empty, or 0 if something is already expired. */
int pool_nearest_timeout_ms(const conn_pool_t *pool)
{
    if (pool->count == 0) return -1;

    const time_t now = time(NULL);
    time_t min_expiry = pool->items[0].keepalive_deadline;

    for (size_t i = 1; i < pool->count; i++) {
        if (pool->items[i].keepalive_deadline < min_expiry) {
            min_expiry = pool->items[i].keepalive_deadline;
        }
    }

    const int diff = (int)(min_expiry - now);
    return (diff <= 0) ? 0 : (diff * 1000);
}


/* Finds a connection by FD, copies it to out_conn, and removes it. */
int pool_remove_by_fd(conn_pool_t *pool, const int fd, conn_t *out_conn)
{
    for (size_t i = 0; i < pool->count; i++) {
        if (pool->items[i].fd == fd) {
            if (out_conn) *out_conn = pool->items[i];

            pool->items[i] = pool->items[pool->count - 1];
            pool->count--;
            return 1; /* Found and removed. */
        }
    }
    return 0; /* Not found. */
}


/* Sweeps for expired connections, unregisters them, and closes them. */
void pool_sweep_expired(conn_pool_t *pool, const io_watcher_t *watcher)
{
    const time_t now = time(NULL);
    size_t i = 0;

    while (i < pool->count) {
        if (pool->items[i].keepalive_deadline <= now) {
            io_watcher_remove(watcher, pool->items[i].fd);

            close_connection(&pool->items[i]);

            pool->items[i] = pool->items[pool->count - 1];
            pool->count--;
        } else {
            i++;
        }
    }
}
