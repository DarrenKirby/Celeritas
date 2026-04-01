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

#include "config.h"
#include "types.h"


/* Use ##__VA_ARGS__ (a common GCC/Clang extension, standard in C23 via __VA_OPT__)
 * to allow calling the macro with OR without additional arguments. */

#define l_debug(log, fmt, ...) \
    log_write(log, LOG_TARGET_EVENT, \
        "%s - %s - pid %d - tid 0x%lx - " fmt "\n", \
         l_priority(L_DEBUG), l_format_datetime(), log->server_pid, get_tid(), \
         ##__VA_ARGS__)

#define l_info(log, fmt, ...) \
    log_write(log, LOG_TARGET_EVENT, \
        "%s - %s - pid %d - tid 0x%lx - " fmt "\n", \
        l_priority(L_INFO), l_format_datetime(), log->server_pid, get_tid(), \
        ##__VA_ARGS__)

#define l_warn(log, fmt, ...) \
    log_write(log, LOG_TARGET_EVENT, \
        "%s - %s - pid %d - tid 0x%lx - " fmt "\n", \
        l_priority(L_WARN), l_format_datetime(), log->server_pid, get_tid(), \
        ##__VA_ARGS__)

#define l_error(log, fmt, ...) \
    log_write(log, LOG_TARGET_EVENT, \
         "%s - %s - pid %d - tid 0x%lx - " fmt "\n", \
         l_priority(L_ERROR), l_format_datetime(), log->server_pid, get_tid(), \
         ##__VA_ARGS__)


logger_t* logger_init(void);
void logger_shutdown(logger_t *log);
char* l_format_datetime(void);
char* l_priority(int priority);
unsigned long get_tid(void);
void early_fatal(const char *msg);
void log_write(const logger_t* log, log_target_t target, const char *fmt, ...);
void log_access(request_ctx_t* ctx, uint64_t latency);


#endif //CELERITAS_LOGGER_H
