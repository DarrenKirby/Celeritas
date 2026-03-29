/*
 * 'src/response.h'
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


#ifndef CELERITAS_RESPONSE_H
#define CELERITAS_RESPONSE_H

#include <stdio.h>

typedef struct request_ctx_t request_ctx_t;


void resp_set_status(request_ctx_t* ctx, int status);
void resp_add_header(request_ctx_t* ctx, const char* name, const char* value);
void resp_add_common_headers(request_ctx_t* ctx);
size_t resp_build_response(request_ctx_t* ctx, char* buf, size_t remaining);
int resp_map_file_to_ctx(request_ctx_t *ctx, const char *filepath);

#endif //CELERITAS_RESPONSE_H
