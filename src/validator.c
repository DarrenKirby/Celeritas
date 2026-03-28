/*
 * 'src/validator.c'
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

#include "validator.h"
#include "types.h"
#include "logger.h"
#include "response.h"

#include <string.h>


int validate_request(request_ctx_t *ctx)
{
    char *uri = ctx->request.h1.uri;

    if (strcmp("GET", ctx->request.h1.method) == 0) {
        ctx->method = M_GET;
    } else if (strcmp("HEAD", ctx->request.h1.method) == 0) {
        ctx->method = M_HEAD;
    } else if (strcmp("PUT", ctx->request.h1.method) == 0) {
        ctx->method = M_PUT;
    } else if (strcmp("DELETE", ctx->request.h1.method) == 0) {
        ctx->method = M_DELETE;
    } else if (strcmp("TRACE", ctx->request.h1.method) == 0) {
        ctx->method = M_TRACE;
    } else if (strcmp("POST", ctx->request.h1.method) == 0) {
        ctx->method = M_POST;
    } else {
        ctx->method = M_INVALID;
        resp_set_status(ctx, 400);
        //ctx->status_code = 405;
        return -1;
    }

    /* Basic Path Traversal Check (The "Dot-Dot-Slash" test). */
    if (strstr(uri, "..")) {
        log_write(&ctx->log->ring, LOG_TARGET_EVENT, "%s - %s - Potential path traversal attempt: %s",
            L_WARN, l_format_datetime(), uri);
        resp_set_status(ctx, 403);
        return -1;
    }

    /* Method Whitelist. */
    const int mask = M_GET|M_HEAD;
    if (!(ctx->method & mask)) {
        log_write(&ctx->log->ring, LOG_TARGET_EVENT, "%s - %s - Request for unsupported method: %s",
            L_DEBUG, l_format_datetime(), uri);
        resp_set_status(ctx, 405);
        return -1;
    }
    resp_set_status(ctx, 200);
    return 0;
}
