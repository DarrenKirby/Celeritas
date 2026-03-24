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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>


long get_ncpu(void) {
    long n_cpu;
    if ((n_cpu = sysconf(_SC_NPROCESSORS_CONF)) == -1) {
        fprintf(stderr, "sysconf failed: %s\n", strerror(errno));
        return 4;
    }
    return n_cpu;
}

config_data read_config(void)
{
    long ncpu = get_ncpu();
    config_data cd;
    cd.http_port = 8080;  /* Whilst testing. */
    cd.https_port = 8443; /* Whilst testing. */
    cd.min_threads = ncpu;
    cd.max_threads = ncpu * 2;
    cd.queue_depth = 256;
    cd.access_log = "/Users/darrenkirby/code/celeritas/logs/access_log";
    cd.event_log = "/Users/darrenkirby/code/celeritas/logs/event_log";
    const pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    cd.mutex = mtx;
    cd.lock_file = nullptr;
    cd.server_pid = -1; /* This will be updated by getpid() call after daemonizing. */
    return cd;
}
