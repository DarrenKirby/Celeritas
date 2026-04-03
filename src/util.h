/*
 * 'src/util.h'
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


#ifndef CELERITAS_UTIL_H
#define CELERITAS_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>


typedef struct request_ctx_t request_ctx_t;


uint32_t next_power_of_2(uint32_t v);
long get_ncpu(void);
uint64_t get_now_ms(void);
uint64_t get_now_us(void);
void early_fatal(const char *msg);
const char* int_to_string(int i);
void get_http_date_now(char *buf, size_t len);
void get_http_date_time_t(char *buf, size_t len, time_t the_time);
bool confirm_header_exists(const request_ctx_t* ctx, const char* header_name);
const char* get_header_value(const request_ctx_t* ctx, const char* key);
void validate_path(const char* path);

void debug_print_request(request_ctx_t* ctx);
void debug_print_response(request_ctx_t* ctx);

#endif //CELERITAS_UTIL_H
