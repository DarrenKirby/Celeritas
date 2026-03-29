/*
 * 'src/config.c'
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

#include "config.h"
#include "util.h"

#include <string.h>
#include <unistd.h>


config_data read_config(void)
{
    const long ncpu = get_ncpu();
    config_data cd;
    cd.http_port = 8080;  /* Whilst testing. */
    cd.https_port = 8443; /* Whilst testing. */
    cd.min_threads = ncpu;
    cd.max_threads = ncpu * 2;
    cd.queue_depth = 256;
    strncpy(cd.access_log, "/Users/darrenkirby/code/celeritas/logs/access_log", PATH_MAX);
    strncpy(cd.event_log, "/Users/darrenkirby/code/celeritas/logs/event_log", PATH_MAX);
    strncpy(cd.doc_root, "/Users/darrenkirby/code/celeritas/www", PATH_MAX);
    strncpy(cd.server_tok, "Celeritas/0.1", 15);
    const pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    cd.mutex = mtx;
    cd.server_pid = -1;  /* This will be updated by getpid() call after daemonizing. */
    cd.max_rx_header = 8192;
    cd.max_tx_header = 8192;
    cd.keepalive_timeout = 10;
    return cd;
}
