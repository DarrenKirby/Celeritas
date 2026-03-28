/*
 * 'src/http_common.c'
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

#include "http_common.h"
#include "handler_static.h"
#include "types.h"
#include "http1.h"
#include "response.h"
#include "socket.h"

#include <unistd.h>
#include <string.h>

#define UPGRADE_REQUIRED 1


const char* http_status_to_string(const int status_code)
{
    switch (status_code) {
        /* Informational. */
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 102: return "Processing";
        case 103: return "Early Hints";

        /* Successful. */
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";
        case 226: return "IM Used";

        /* Redirection. */
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 306: return "Unused";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";

        /* Client Error. */
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Content Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 418: return "I'm a teapot";
        case 421: return "Misdirected Request";
        case 422: return "Unprocessable Content";
        case 423: return "Locked";
        case 424: return "Failed Dependency";
        case 425: return "Too Early";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 451: return "Unavailable For Legal Reasons";

        /* Server Error. */
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 506: return "Variant Also Negotiates";
        case 507: return "Insufficient Storage";
        case 508: return "Loop Detected";
        case 510: return "Not Extended";
        case 511: return "Network Authentication Required";

        case 0:
        default: return "Unspecified";
    }
}


int process_ingress(request_ctx_t *ctx)
{
    const int status = read_http_headers(ctx);

    if (status != 0) {
        switch (status) {
            /* Header too large. */
            case -3:
                ctx->status_code = 431;
                return -1;
            /* Socket error - set to internal server error. */
            case -1:
                ctx->status_code = 500;
                return -1;
            /* Failed to parse request headers, some unspecified client error. */
            default:
                ctx->status_code = 400;
                return -1;
        }
    }

    /* Determine the HTTP version of the request.
     * Only run if not already known. */
    if (ctx->conn->protocol == PROTO_UNKNOWN) {
        demux_protocol(ctx);
    }

    if (ctx->conn->protocol == PROTO_HTTP2) {
        ctx->status_code = 505; /* "HTTP Version Not Supported" (for now). */
        return -1;
    }

    const int p_status = parse_http1(ctx, ctx->header_buffer, ctx->header_buffer_size);
    if (p_status == UPGRADE_REQUIRED) {
        // Handle h2c logic here later
        ctx->status_code = 101;
        return -1;
    }
    if (p_status != 0) {
        ctx->status_code = 400;
        return -1;
    }

    return 0;
}


void route_request(request_ctx_t *ctx)
{
    /* If a previous stage (validation) already set an error status,
     * route to the error handler. */
    if (ctx->status_code >= 400) {
        ctx->handler = handle_error;
        return;
    }
    /* Right now, only handlers are static and error. */
    ctx->handler = handle_get_head;
}


void send_response(request_ctx_t *ctx)
{
    /* Build the header string into the buffer. */
    const size_t header_len = resp_build_response(ctx, ctx->response.header_buffer, 8192);

    /* Send the headers. */
    write(ctx->conn->fd, ctx->response.header_buffer, header_len);

    /* Send the body IF it's a GET request, and it has data
     * (HEAD requests MUST NOT have a body per RFC 9110). */
    if (ctx->method == M_GET && ctx->response.body_data != NULL && ctx->response.body_len > 0) {
        write(ctx->conn->fd, ctx->response.body_data, ctx->response.body_len);
    }
}


bool should_keep_alive(const request_ctx_t *ctx)
{
    /* If we sent a "Connection: close" header (e.g. in handle_error), don't keep alive. */
    for (int i = 0; i < ctx->response.header_count; i++) {
        if (strcasecmp(ctx->response.headers[i].key, "Connection") == 0) {
            if (strcasecmp(ctx->response.headers[i].value, "close") == 0) return false;
        }
    }

    /* HTTP/1.1 defaults to keep-alive. */
    if (ctx->conn->protocol == PROTO_HTTP11) return true;

    return false;
}
