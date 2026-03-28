/*
 * 'src/logger.h'
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


#ifndef CELERITAS_LOGGER_H
#define CELERITAS_LOGGER_H

#include "types.h"


void l_debug(logger_t* log, char* s);
void l_info(logger_t* log, char* s);
void l_warn(logger_t* log, char* s);
void l_error(logger_t* log, char* s);
void logger_init(void);
void logger_shutdown(logger_t *log);
char* l_format_datetime(void);
char* l_priority(int priority);
void *get_tid(void);
void log_write(log_ring_t *ring, log_target_t target, const char *fmt, ...);
void log_access(request_ctx_t* ctx, uint64_t latency);


#endif //CELERITAS_LOGGER_H
