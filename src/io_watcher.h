/*
 * 'src/io_watcher.h'
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


#ifndef CELERITAS_IO_WATCHER_H
#define CELERITAS_IO_WATCHER_H

#include "types.h"


io_watcher_t *io_watcher_create(logger_t *log);
void io_watcher_destroy(io_watcher_t *w);

/* Register fd for one-shot readability notification. */
int io_watcher_add(const io_watcher_t *w, int fd, void *userdata);

/* Deregister fd (call before closing on error/timeout). */
int io_watcher_remove(const io_watcher_t *w, int fd);

/* Wait up to timeout_ms. Returns number of events, or -1 on error. */
int io_watcher_wait(const io_watcher_t *w, io_event_t *events,
    int max_events, int timeout_ms);

int pool_init(conn_pool_t *pool, size_t max_conns);
int pool_add(conn_pool_t *pool, conn_t new_conn);
void pool_remove_at(conn_pool_t *pool, size_t index);
int pool_nearest_timeout_ms(const conn_pool_t *pool);
int pool_remove_by_fd(conn_pool_t *pool, int fd, conn_t *out_conn);
void pool_sweep_expired(conn_pool_t *pool, const io_watcher_t *watcher, logger_t log);

#endif //CELERITAS_IO_WATCHER_H
