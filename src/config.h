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
#include <limits.h>

#define MAX_NAME_LEN 256


typedef struct logger_t logger_t;
typedef struct config_data config_data;


struct config_data {
    /* Administrative. */
    uint16_t http_port;        /* Plain HTTP listener port. */
    uint16_t https_port;       /* HTTPS listener port. */
    char server_tok[20];       /* Server header value. */
    char server_user[MAX_NAME_LEN];
    char server_group[MAX_NAME_LEN];
    /* Security/DoS. */
    uint16_t max_url_size;          /* Max length of the URL component of a request. */
    uint16_t max_rx_header;         /* Max limit for request header size. */
    uint16_t max_tx_header;         /* Max limit for response header size. */
    uint16_t keepalive_timeout;     /* Number of seconds to wait for a request before closing connection. */
    bool dir_listing;               /* Enable directory listing for static handler. */
    char tls_cert_path[PATH_MAX];   /* Path to the TLS/SSL server certificate. */
    char tls_key_path[PATH_MAX];    /* Path to the TLS/SSL server key. */
    /* Performance tuning. */
    uint16_t log_queue_size;        /* Max messages in log queue before dropping. */
    uint16_t conn_queue_size;       /* Max pending connections. */
    uint16_t worker_threads;        /* Initial number of worker threads in pool. */
    /* Filesystem locations. */
    char access_log[PATH_MAX];      /* Path to the access log file. */
    char event_log[PATH_MAX];       /* Path to the event log file. */
    char doc_root[PATH_MAX];        /* The directory from which to serve content. */
    char lock_file_path[PATH_MAX];  /* The directory in which to write the runtime lock file. */
};


void init_config(void);
void cleanup_config(void);
void reload_configuration(const logger_t* log);

#endif //CELERITAS_CONFIG_H

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
lock_file_path = /var/run/
doc_root = /var/www/html/
tls_cert_path = /etc/ssl/celeritas.crt
tls_key_path = /etc/ssl/celeritas.key

# Limits
max_rx_header_size = 8192
max_tx_header_size = 8192
max_url_size = 2048
keepalive_timeout = 10

# Queue sizes (will be rounded up to the next power of 2)
log_queue_size = 512
connection_queue_size = 512

# User/group
server_user = www
server_group = www

 * ----------------------------------------------------- */
