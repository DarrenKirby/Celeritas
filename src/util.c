/*
 * 'src/util.c'
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

#include "util.h"
#include "types.h"
#include "http_common.h"
#include "logger.h"
#include "socket.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>


long get_ncpu(void)
{
    long n_cpu;
    if ((n_cpu = sysconf(_SC_NPROCESSORS_CONF)) == -1) {
        fprintf(stderr, "sysconf failed: %s\n", strerror(errno));
        return 4;
    }
    return n_cpu;
}


uint64_t get_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}


uint64_t get_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}


/* Fills a buffer with the current IMF-fixdate format time. */
void get_http_date_now(char *buf, size_t len)
{
    const time_t now = time(nullptr);
    const struct tm *tm_info = gmtime(&now);
    strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
}


/* Fills a buffer with a supplied time_t IMF-fixdate format time for http messages. */
void get_http_date_time_t(char *buf, size_t len, time_t the_time)
{
    const struct tm *tm_info = gmtime(&the_time);
    strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
}


/* Formats an integer as a string for building headers. */
const char* int_to_string(const int i)
{
    static char buf[32];
    snprintf(buf, 32, "%d", i);
    return buf;
}


void debug_print_request(request_ctx_t* ctx)
{
    l_debug(ctx->log, "Start request debug printout\n");
    FILE* fd = fopen(conf_data.event_log, "a");
    fprintf(fd, "Request from %s; remote port %d\n", ctx->conn->remote_ip, ctx->conn->remote_port);
    fprintf(fd, " Method: %s\n", ctx->request.h1.method);
    fprintf(fd, "    URI: %s\n", ctx->request.h1.uri);
    fprintf(fd, "Version: %s\n\n", ctx->request.h1.version);
    fprintf(fd, "Headers: (%d total)\n", ctx->request.h1.header_count);
    for (int i = 0; i < ctx->request.h1.header_count; i++) {
        fprintf(fd, "%20s -> %s\n", ctx->request.h1.headers[i].key,
                                  ctx->request.h1.headers[i].value);

    }
    fclose(fd);
    l_debug(ctx->log, "End request debug printout\n");
}


void debug_print_response(request_ctx_t* ctx)
{
    l_debug(ctx->log, "Start response debug printout\n");
    FILE* fd = fopen(conf_data.event_log, "a");
    fprintf(fd, "Status code: %d %s\n", ctx->status_code, http_status_to_string(ctx->status_code));
    fprintf(fd, "Method: %d\n", ctx->method);
    fprintf(fd, "Headers: (%d total)\n", ctx->response.header_count);
    for (int i = 0; i < ctx->response.header_count; i++) {
        fprintf(fd, "%20s -> %s\n", ctx->response.headers[i].key,
                                  ctx->response.headers[i].value);

    }
    fprintf(fd, "Body length: %zu\n", ctx->response.body_len);
    fprintf(fd, "Header buffer:\n");
    fprintf(fd, "%s\n", (char*)ctx->response.header_buffer);
    fclose(fd);
    l_debug(ctx->log, "End response debug printout\n");
}
