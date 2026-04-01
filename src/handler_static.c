/*
 * 'src/handler_static.c'
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

#include "handler_static.h"
#include "types.h"
#include "response.h"
#include "util.h"
#include "mime.h"
#include "http_common.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>

#include "threadpool.h"


/* Resolves a URI to a physical path on disk. */
void resolve_path(const char *docroot, const char *uri, char *out_path, size_t out_len) {
    /* Ensure exactly one slash between docroot and uri. */
    const size_t root_len = strlen(docroot);

    /* Remove trailing slash from docroot if it exists. */
    const int root_has_slash = (docroot[root_len - 1] == '/');
    /* Check if URI has a leading slash. */
    const int uri_has_slash = (uri[0] == '/');

    /* Normalize the combined path with one slash. */
    if (root_has_slash && uri_has_slash) {
        snprintf(out_path, out_len, "%s%s", docroot, uri + 1);
    } else if (!root_has_slash && !uri_has_slash) {
        snprintf(out_path, out_len, "%s/%s", docroot, uri);
    } else {
        snprintf(out_path, out_len, "%s%s", docroot, uri);
    }

    /*  If the path ends in a slash, append index.html. */
    const size_t new_len = strlen(out_path);
    if (out_path[new_len - 1] == '/') {
        strncat(out_path, "index.html", out_len - new_len - 1);
    }
}


void handle_error(request_ctx_t *ctx)
{
    resp_add_common_headers(ctx);
    resp_add_header(ctx, "Connection", "close");
    resp_add_header(ctx, "Content-Type", "text/html");
    if (ctx->status_code == SC_405_METHOD_NOT_ALLOWED) {
        resp_add_header(ctx, "Allow", "GET, HEAD, OPTIONS");
    }

    /* Generate a simple HTML body based on the status code. */
    char *body = malloc(512);
    const int len = snprintf(body, 512,
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><p>Celeritas Server</p></body></html>",
        ctx->status_code, http_status_to_string(ctx->status_code),
        ctx->status_code, http_status_to_string(ctx->status_code));

    /* Attach to context. */
    ctx->response.body_data = body;
    ctx->response.body_len = len;

    resp_add_header(ctx, "Content-Length", int_to_string(len));

    /* NOTE: Because this is heap allocated, cleanup_request_resources
     * needs to know to free() it instead of munmap() it! */
}



void handle_static(request_ctx_t *ctx)
{
    switch (ctx->method) {
        case M_GET:
        case M_HEAD:
            return handle_get_head(ctx);
        case M_OPTIONS:
            return handle_options(ctx);
        default:
            ;
    }
}


void handle_options(request_ctx_t *ctx)
{
    resp_add_common_headers(ctx);
    resp_add_header(ctx, "Accept-Ranges",  "bytes");
    resp_add_header(ctx, "Allow", "GET, HEAD, OPTIONS");
    resp_add_header(ctx, "Content-Length", int_to_string(0));
    resp_add_header(ctx, "Connection", "keep-alive");
}


void handle_get_head(request_ctx_t* ctx)
{
    char local_file[PATH_MAX];
    char doc_root[PATH_MAX];

    /* Lock to get conf values. */
    THR_OK(pthread_rwlock_rdlock(&config_lock));
    strncpy(doc_root, conf_data->doc_root, PATH_MAX);
    THR_OK(pthread_rwlock_unlock(&config_lock));

    resolve_path(doc_root, ctx->request.h1.uri, local_file, PATH_MAX);

    /* Stat the file for size and last modified. */
    struct stat sb;

    if (stat(local_file, &sb) == -1) {
        resp_set_status(ctx, SC_404_NOT_FOUND);
        handle_error(ctx);
        return;
    }

    ctx->response.body_len = sb.st_size;
    char last_mod[30];
    get_http_date_time_t(last_mod, 30, sb.st_mtime);

    resp_add_common_headers(ctx);
    resp_add_header(ctx, "Content-Type", get_mime_type(local_file));
    resp_add_header(ctx, "Last-Modified", last_mod);
    resp_add_header(ctx, "Content-Length", int_to_string((int)sb.st_size));
    resp_add_header(ctx, "Connection", "keep-alive");
    resp_add_header(ctx, "Content-Language", "en-CA");

    if (ctx->method == M_GET && sb.st_size > 0) {
        resp_map_file_to_ctx(ctx, local_file);
        ctx->response.is_mmap = true;
    }
}


void cleanup_request_resources(const request_ctx_t* ctx)
{
    if (ctx->response.body_data != NULL) {
        if (ctx->response.is_mmap) {
            munmap(ctx->response.body_data, ctx->response.body_len);
        } else {
            /* If the body wasn't mmapp'ed, it was heap allocated. */
            free(ctx->response.body_data);
        }
    }
}
