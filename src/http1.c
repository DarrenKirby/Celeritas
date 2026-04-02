/*
 * 'src/http1.c'
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
#include "logger.h"

#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#define MAX_HEADER_SIZE 8192


int read_http_headers(request_ctx_t* ctx)
{
    size_t received = 0;
    const int fd = ctx->conn->fd;
    char* buf = ctx->header_buffer;

    while (received < MAX_HEADER_SIZE - 1) {
        /* Read into the buffer starting at the first 'empty' spot. */
        const ssize_t n = recv(fd, buf + received, MAX_HEADER_SIZE - 1 - received, 0);

        if (n < 0) {
            /* Interrupted by signal, try again. */
            if (errno == EINTR) continue;
            /* Actual socket error. */
            return -1;
        }
        /* Client closed connection prematurely. */
        if (n == 0) return -2;

        received += n;
        buf[received] = '\0';

        /* Check if we read the end-of-header marker. */
        if (strstr(buf, "\r\n\r\n")) {
            ctx->header_buffer_size = received;
            return 0;
        }
    }
    /* Header too large (Potential DoS). */
    return -3;
}


int parse_http1(request_ctx_t *ctx, const char *buf, const size_t len)
{
    const char *p = buf;
    const char *end = buf + len;
    parse_step_t state = P_STATE_METHOD;

    char *dest = ctx->request.h1.method;
    size_t count = 0;
    int header_idx = 0;

    while (p < end && state != P_STATE_DONE) {
        switch (state) {
            case P_STATE_METHOD:
                if (*p == ' ') {
                    dest[count] = '\0';
                    state = P_STATE_URI;
                    dest = ctx->request.h1.uri;
                    count = 0;
                } else if (count < 15) {
                    dest[count++] = *p;
                }
                break;

            case P_STATE_URI:
                if (*p == ' ') {
                    dest[count] = '\0';
                    state = P_STATE_VERSION;
                    dest = ctx->request.h1.version;
                    count = 0;
                } else if (count < 4095) {
                    dest[count++] = *p;
                }
                break;

            case P_STATE_VERSION:
                if (*p == '\r') { /* Ignore \r, wait for \n. */ }
                else if (*p == '\n') {
                    dest[count] = '\0';
                    state = P_STATE_HEADER_KEY;
                    header_idx = 0;
                    dest = ctx->request.h1.headers[header_idx].key;
                    count = 0;
                } else if (count < 15) {
                    dest[count++] = *p;
                }
                break;

            case P_STATE_HEADER_KEY:
                if (*p == ':') {
                    dest[count] = '\0';
                    p++; /* Skip space after colon. */
                    while (p < end && *p == ' ') p++;
                    state = P_STATE_HEADER_VALUE;
                    dest = ctx->request.h1.headers[header_idx].value;
                    count = 0;
                    p--; /* Offset the p++ at end of loop. */
                } else if (*p == '\r') { /* Wait for \n. */ }
                else if (*p == '\n') {
                    state = P_STATE_BODY; /* Empty line found. */
                } else if (count < 127) {
                    dest[count++] = *p;
                }
                break;

            case P_STATE_HEADER_VALUE:
                if (*p == '\r') { /* Wait for \n. */ }
                else if (*p == '\n') {
                    dest[count] = '\0';
                    header_idx++;
                    ctx->request.h1.header_count = header_idx;

                    if (header_idx >= 128) {
                        state = P_STATE_BODY; /* Too many headers. */
                    } else {
                        state = P_STATE_HEADER_KEY;
                        dest = ctx->request.h1.headers[header_idx].key;
                        count = 0;
                    }
                } else if (count < 4095) {
                    dest[count++] = *p;
                }
                break;

            default:
                state = P_STATE_DONE;
                break;
        }
        p++;
    }

    for (int i = 0; i < ctx->request.h1.header_count; i++) {
        if (strcasecmp(ctx->request.h1.headers[i].key, "Upgrade") == 0) {
            if (strstr(ctx->request.h1.headers[i].value, "h2c")) {
                /* Found an upgrade request in headers. */
                l_info(ctx->log, "h2c upgrade detected");
                return 1;
            }
        }
    }

    if (strcasecmp(ctx->request.h1.version, "HTTP/1.0") == 0) {
        ctx->conn->protocol = PROTO_HTTP10;
    }

    return 0;
}
