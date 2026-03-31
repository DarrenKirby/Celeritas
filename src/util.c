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
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>


/* Rounds a 32-bit integer up to the next highest power of 2.
 * This is for enforcing power of 2 values for the log buffer size. */
uint32_t next_power_of_2(uint32_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;

    /* Catch 0 or sizes that overflowed */
    return (v == 0) ? 1 : v;
}


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
void get_http_date_now(char *buf, const size_t len)
{
    struct tm tm_info; /* Stack allocated, thread-safe */
    const time_t now = time(nullptr);

    gmtime_r(&now, &tm_info);
    strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", &tm_info);
}


/* Fills a buffer with a supplied time_t IMF-fixdate format time for http messages. */
void get_http_date_time_t(char *buf, const size_t len, const time_t the_time)
{
    struct tm tm_info; /* Stack allocated, thread-safe */

    gmtime_r(&the_time, &tm_info);
    strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", &tm_info);
}


/* Formats an integer as a string for building headers. */
const char* int_to_string(const int i)
{
    static char buf[32];
    snprintf(buf, 32, "%d", i);
    return buf;
}


bool confirm_header_exists(const request_ctx_t* ctx, const char* header_name)
{
    for (int i = 0; i < ctx->request.h1.header_count; i++) {
        if (strcasecmp(ctx->request.h1.headers[i].key, header_name) == 0) {
            return true;
        }
    }
    return false;
}


/* Case-insensitive search for a request header key. Returns
 * null if the header does not exist. */
const char* get_header_value(const request_ctx_t* ctx, const char* key)
{
    for (int i = 0; i < ctx->request.h1.header_count; i++) {
        if (strcasecmp(ctx->request.h1.headers[i].key, key) == 0) {
            return ctx->request.h1.headers[i].value;
        }
    }
    return nullptr;
}


/* Helper function to format error messages before dying. */
static void die_with_error(const char* context) {
    char err_msg[512];
    snprintf(err_msg, sizeof(err_msg), "%s: %s", context, strerror(errno));
    early_fatal(err_msg);
}


/* Helper to mimic 'mkdir -p' safely with umask(0). */
static void create_dir_recursive(const char* dir_path) {
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s", dir_path) >= (int)sizeof(tmp)) {
        early_fatal("Directory path too long");
    }

    char *p = tmp;
    /* Skip the leading slash if absolute path. */
    if (*p == '/') p++;

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0'; /* Temporarily truncate string. */

            /* Explicitly use 0755 to prevent world-writable directories. */
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                die_with_error("Failed to create intermediate directory");
            }
            *p = '/'; /* Restore slash. */
        }
    }

    /* Create the final directory. */
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        die_with_error("Failed to create parent directory");
    }
}


void validate_path(const char* path)
{
    if (!path || *path == '\0') {
        early_fatal("Invalid path: Path is NULL or empty");
    }

    struct stat st;

    /* Check if the exact path exists. */
    if (stat(path, &st) == 0) {
        /* Requirement: It must be a file, not a directory. */
        if (!S_ISREG(st.st_mode)) {
            early_fatal("Path exists but is not a regular file");
        }

        /* It is readable/writable by calling process. */
        if (access(path, R_OK | W_OK) != 0) {
            die_with_error("File exists but lacks read/write permissions");
        }

        return; /* Success. */
    }

    /* If stat failed for a reason other than "does not exist", that's fatal. */
    if (errno != ENOENT) {
        die_with_error("Failed to stat path");
    }

    /* File does not exist past this point. */

    char dir_path[PATH_MAX];
    if (snprintf(dir_path, sizeof(dir_path), "%s", path) >= (int)sizeof(dir_path)) {
        early_fatal("File path too long");
    }

    /* Isolate the parent directory path. */
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash != NULL) {
        if (last_slash == dir_path) {
            dir_path[1] = '\0'; /* The parent is the root directory "/". */
        } else {
            *last_slash = '\0'; /* Truncate to get just the directory path. */
        }

        /* Check if the parent directory exists. */
        struct stat dir_st;
        if (stat(dir_path, &dir_st) == 0) {
            if (!S_ISDIR(dir_st.st_mode)) {
                early_fatal("Parent path exists but is not a directory");
            }
        } else {
            if (errno == ENOENT) {
                /* Create the path mkdir -p style. */
                create_dir_recursive(dir_path);
            } else {
                die_with_error("Failed to stat parent directory");
            }
        }

        /* Ensure calling process can create a new file there.
         * W_OK allows creating a file, X_OK allows entering the directory. */
        if (access(dir_path, W_OK | X_OK) != 0) {
            die_with_error("Cannot create file in parent directory (insufficient permissions)");
        }
    } else {
        /* No slash means the file is in the current working directory. */
        if (access(".", W_OK | X_OK) != 0) {
            die_with_error("Cannot create file in current directory (insufficient permissions)");
        }
    }
    /* Everything successful, fall off the end. */
}


void debug_print_request(request_ctx_t* ctx)
{
    l_debug(ctx->log, "Start request debug printout\n");
    FILE* fd = fopen(conf_data->event_log, "a");
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
    FILE* fd = fopen(conf_data->event_log, "a");
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
