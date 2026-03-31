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

#include "util.h"


void validate_request(request_ctx_t *ctx)
{
    char *uri = ctx->request.h1.uri;
    const char* method = ctx->request.h1.method;

    if (strcmp("GET", method) == 0) {
        ctx->method = M_GET;
    } else if (strcmp("HEAD", method) == 0) {
        ctx->method = M_HEAD;
    } else if (strcmp("PUT", method) == 0) {
        ctx->method = M_PUT;
    } else if (strcmp("DELETE", method) == 0) {
        ctx->method = M_DELETE;
    } else if (strcmp("TRACE", method) == 0) {
        ctx->method = M_TRACE;
    } else if (strcmp("POST", method) == 0) {
        ctx->method = M_POST;
    } else if (strcmp("OPTIONS", method) == 0) {
        ctx->method = M_OPTIONS;
    } else {
        ctx->method = M_INVALID;
        l_warn(ctx->log, "unknown method: %s", method);
        resp_set_status(ctx, SC_400_BAD_REQUEST);
        return;
    }

    /* Check for Host header. */
    if (!confirm_header_exists(ctx, "Host")) {
        resp_set_status(ctx, SC_400_BAD_REQUEST);
        l_warn(ctx->log, "request does not contain Host header");
        return;
    }

    /* Basic path traversal check. */
    if (strstr(uri, "..")) {
        l_warn(ctx->log, "potential path traversal attempt: %s", uri);
        resp_set_status(ctx, SC_403_FORBIDDEN);
        return;
    }

    /* Method Whitelist. */
    // ReSharper disable once CppVariableCanBeMadeConstexpr
    const int mask = M_GET|M_HEAD|M_OPTIONS;
    if (!(ctx->method & mask)) {
        l_warn(ctx->log, "request for unsupported method: %s", ctx->request.h1.method);
        resp_set_status(ctx, SC_405_METHOD_NOT_ALLOWED);
        return;
    }

    resp_set_status(ctx, SC_200_OK);
}
