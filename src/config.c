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


#include "util.h"
#include "config.h"
#include "types.h"
#include "logger.h"
#include "threadpool.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>


void load_config_defaults(config_data *cd)
{
    const long ncpu = get_ncpu();

    /* Start with some defaults that can be overridden by
     * values in the config file, or passed on the CLI. */

    cd->http_port = 8080;  /* Whilst testing. */
    cd->https_port = 8443; /* Whilst testing. */
    cd->min_threads = ncpu;
    cd->max_threads = ncpu * 2;
    cd->queue_depth = 256;
    strncpy(cd->access_log, "../logs/access_log", PATH_MAX);
    strncpy(cd->event_log, "../logs/event_log", PATH_MAX);
    strncpy(cd->doc_root, "../www", PATH_MAX);
    strncpy(cd->server_tok, "Celeritas/0.1", 15);
    cd->server_pid = -1;  /* This will be updated by getpid() call after daemonizing. */
    cd->max_rx_header = 8192;
    cd->max_tx_header = 8192;
    cd->keepalive_timeout = 10;
}


void init_config(void) {
    /* Initialize the attribute object. */
    pthread_rwlockattr_t attr;
    if (pthread_rwlockattr_init(&attr) != 0) {
        early_fatal("Failed to initialize rwlock attributes");
    }

    /* Set the writer-preference attribute (Linux/glibc only). */
#if defined(__linux__)
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif

    /* Initialize the actual lock using the configured attributes. */
    if (pthread_rwlock_init(&config_lock, &attr) != 0) {
        early_fatal("Failed to initialize configuration rwlock");
    }

    /* Destroy the attribute object immediately. */
    pthread_rwlockattr_destroy(&attr);

    /* Allocate the initial configuration structure. */
    conf_data = calloc(1, sizeof(config_data));
    if (!conf_data) {
        early_fatal("Failed to allocate memory for configuration");
    }
    
    load_config_defaults(conf_data);
}


void cleanup_config(void) 
{
    pthread_rwlock_destroy(&config_lock);
    if (conf_data) {
        free(conf_data);
        conf_data = nullptr;
    }
}


void reload_configuration()
{
    /* Do the slow work entirely outside the lock. */
    config_data *new_config = malloc(sizeof(config_data));
    load_config_defaults(new_config);
    // if (!parse_and_validate_config_file("/etc/celeritas.conf", new_config)) {
    //     free(new_config);
    //     return; // Parsing failed, keep the old config
    // }

    /* ### Critical section start. ### */
    THR_OK(pthread_rwlock_wrlock(&config_lock));
    config_data *old_config = conf_data;
    conf_data = new_config;
    THR_OK(pthread_rwlock_unlock(&config_lock));
    /* ### Critical section end. ### */

    free(old_config);
}


/* Example config file format including default values
 * -------------------------------------------------- *

# Ports
http_port = 80
https_port = 443

# Threads
worker_threads = 12

# Paths
access_log = /var/log/celeritas/access_log
event_log = /var/log/celeritas/event_log
run_file = /var/run/celeritas.pid
doc_root = /var/www/

# limits
max_rx_header_size = 8192
max_tx_header_size = 8192
max_url_size = 2048
keepalive_timeout = 10

# Queue sizes (will be rounded up to the next power of 2)
log_queue_size =
connection_queue_size =

# Server identification (This will be appended to "Celeritas/0.1")
server_id =

 * ----------------------------------------------------- */
