/*
 * 'src/server.h'
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


#ifndef CELERITAS_SERVER_H
#define CELERITAS_SERVER_H

#include "threadpool.h"

#include <stdnoreturn.h>


typedef struct {
    logger_t *logger;
    sigset_t *sig_mask;
} sig_handler_t;


void daemonize(void);
int already_running(const char* lockfile_name, int elfd);
int drop_privileges(const char *username, const char *groupname, int fd);
noreturn void server_shutdown(logger_t* log, int status);
void *thr_sig_handler(void *arg);
int lockfile(int fd);
void resolve_config_path(int argc, char *argv[]);
SSL_CTX *init_ssl_context(const char *cert_path, const char *key_path);

#endif //CELERITAS_SERVER_H
