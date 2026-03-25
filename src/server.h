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

#include "logger.h"
#include "threadpool.h"

#include <stdnoreturn.h>


typedef struct {
    logger_t *logger;
    sigset_t *sig_mask;
} sig_handler_t;


void daemonize(void);
int already_running(char* lockfile_name, logger_t* log);
void reread_config(logger_t* log);
noreturn void server_shutdown(logger_t* log, int status);
void *thr_sig_handler(void *arg);
int lockfile(int fd);

#endif //CELERITAS_SERVER_H
