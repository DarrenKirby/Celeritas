/*
 * 'src/response.c'
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

#include "types.h"
#include "http_common.h"
#include "util.h"
#include "config.h"

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>


void resp_set_status(request_ctx_t* ctx, const int status)
{
    ctx->status_code = status;
}


void resp_add_header(request_ctx_t* ctx, const char* name, const char* value)
{
    const int header_idx = ctx->response.header_count;
    char* k = ctx->response.headers[header_idx].key;
    char* v = ctx->response.headers[header_idx].value;
    strncpy(k, name, 128);
    strncpy(v, value, 4096);
    ctx->response.header_count++;
}


void resp_add_common_headers(request_ctx_t* ctx)
{
    char now[30];
    get_http_date_now(now, 30);
    resp_add_header(ctx, "Server", conf_data->server_tok);
    resp_add_header(ctx, "Date", now);
}


size_t resp_build_response(request_ctx_t* ctx, char* buf, size_t remaining)
{
    /* NOTE: Overflow code commented out, as currently,
     * we have full control over length and content of
     * response headers, they will not overflow a 8192 byte buffer. */
    char *p = buf;
    size_t total_size = 0;

    /* Status line. */
    size_t n = snprintf(p, remaining, "HTTP/1.1 %d %s\r\n",
                     ctx->status_code, http_status_to_string(ctx->status_code));
    //if (n >= remaining) goto overflow; // Check for truncation
    p += n;
    total_size += n;
    remaining -= n;

    /* Loop through headers. */
    for (int i = 0; i < ctx->response.header_count; i++) {
        n = snprintf(p, remaining, "%s: %s\r\n",
                     ctx->response.headers[i].key, ctx->response.headers[i].value);
        //if (n >= remaining) goto overflow;
        p += n;
        total_size += n;
        remaining -= n;
    }

    /* The "Great Divider". */
    n = snprintf(p, remaining, "\r\n");
    //\\if (n >= remaining) goto overflow;
    total_size += n;
    buf[total_size] = '\0';
    return total_size;
}


int resp_map_file_to_ctx(request_ctx_t *ctx, const char *filepath)
{
    const int fd = open(filepath, O_RDONLY);
    if (fd < 0) return -1;

    /* mmap(addr, length, prot, flags, fd, offset) */
    ctx->response.body_data = mmap(NULL,
                                   ctx->response.body_len,
                                   PROT_READ,
                                   MAP_PRIVATE,
                                   fd,
                                   0);

    /* The mapping persists after the FD is closed. */
    close(fd);

    if (ctx->response.body_data == MAP_FAILED) {
        ctx->response.body_data = NULL;
        ctx->response.body_len = 0;
        return -1;
    }
    return 0;
}
