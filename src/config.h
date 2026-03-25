/*
 * 'src/config.h'
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


#ifndef CELERITAS_CONFIG_H
#define CELERITAS_CONFIG_H

#include <stdint.h>
#include <pthread.h>
#include <sys/syslimits.h>
#include <sys/types.h>

typedef struct Config_Data{
    uint16_t queue_depth;      /* Max pending connections. */
    uint16_t http_port;        /* Plain HTTP listener port. */
    uint16_t https_port;       /* HTTPS listener port. */
    uint16_t min_threads;      /* Minimum worker threads in pool. */
    uint16_t max_threads;      /* Maximum worker threads in pool. */
    char access_log[PATH_MAX]; /* Path to the access log file. */
    char event_log[PATH_MAX];  /* Path to the event log file. */
    char lock_file[PATH_MAX];  /* The runtime lock file to stop multiple server instances. */
    pid_t server_pid;          /* The pid of the running server. */
    pthread_mutex_t mutex;     /* Lock for thread synchronization. */
} config_data;

config_data read_config(void);

#endif //CELERITAS_CONFIG_H
