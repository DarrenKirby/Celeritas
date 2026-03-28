/*
 * 'src/http_common.h'
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


#ifndef CELERITAS_HTTP_COMMON_H
#define CELERITAS_HTTP_COMMON_H

typedef struct request_ctx_t request_ctx_t;


const char* http_status_to_string(int status_code);
int process_ingress(request_ctx_t *ctx);
void route_request(request_ctx_t *ctx);
void send_response(request_ctx_t *ctx);
bool should_keep_alive(const request_ctx_t *ctx);

#endif //CELERITAS_HTTP_COMMON_H
