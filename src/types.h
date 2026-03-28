/*
 * 'src/types.h'
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


#ifndef CELERITAS_TYPES_H
#define CELERITAS_TYPES_H

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <openssl/types.h>

#define LOG_LINE_MAX  2048
#define LOG_RING_SIZE 512   /* must be power of 2 for cheap modulo */
#define HEADER_BUFFER_SIZE 8192


typedef struct log_entry_t log_entry_t;
typedef struct log_ring_t log_ring_t;
typedef struct logger_t logger_t;
typedef struct conn_t conn_t;
typedef struct request_ctx_t request_ctx_t;
typedef struct kv_t kv_t;
typedef struct http1_req_t http1_req_t;
typedef struct h2_stream_t h2_stream_t;


typedef enum {
    PROTO_UNKNOWN,
    PROTO_HTTP11,
    PROTO_HTTP2
} protocol_t;


typedef enum {
    L_DEBUG,
    L_INFO,
    L_WARN,
    L_ERROR,
    L_ACCESS
} log_level_t;


typedef enum {
    LOG_TARGET_ACCESS,
    LOG_TARGET_EVENT
} log_target_t;


typedef enum {
    P_STATE_METHOD,
    P_STATE_URI,
    P_STATE_VERSION,
    P_STATE_HEADER_KEY,
    P_STATE_HEADER_VALUE,
    P_STATE_BODY,
    P_STATE_DONE
} parse_step_t;


typedef enum {
    M_GET = 1 << 0,
    M_HEAD = 1 << 1,
    M_POST = 1 << 2,
    M_PUT = 1 << 3,
    M_DELETE = 1 << 4,
    M_TRACE = 1 << 5,
    M_CONNECT = 1 << 6,
    M_DISCONNECT = 1 << 7,
    M_CLOSE = 1 << 8,
    M_INVALID = 1 << 9
} http_method_t;


struct log_entry_t {
    log_target_t target;
    int          len;
    char         line[LOG_LINE_MAX];
};


struct log_ring_t {
    log_entry_t     entries[LOG_RING_SIZE];
    int             head;       /* logger thread reads from here  */
    int             tail;       /* producers write to here        */
    int             count;      /* current number of entries      */
    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;  /* logger sleeps on this          */
    pthread_cond_t  not_full;   /* producers sleep on this if full */
};


struct logger_t {
    int        access_fd; /* Access log file descriptor. */
    int        event_fd;  /* Error log file descriptor. */
    log_ring_t ring;      /* The log entry ring buffer. */
    pthread_t  thread;    /* The singleton logger thread ID. */
    int        shutdown;  /* Set to 1 by main thread to signal exit */
};


struct kv_t {
    char key[128];
    char value[4096];
};


struct http1_req_t {
    char     method[16];          /* GET, POST, etc. */
    char     uri[4096];           /* Raw request URI. */
    char     version[16];         /* HTTP/1.1 */
    kv_t     headers[128];        /* Array of key/value  */
    int      header_count;        /* Number of headers in request. */
    uint8_t *body;                /* Heap-allocated body. */
    size_t   body_len;            /* Size of body in bytes. */
};


struct h2_stream_t {
    int foo;
};


struct request_ctx_t {
    conn_t *conn;      /* Connection struct from the work queue. */
    logger_t *log;     /* Pointer to the logging mechanism. */

    /* Buffer for the socket read. */
    char header_buffer[HEADER_BUFFER_SIZE];
    size_t header_buffer_size;

    /* Pipeline State. */
    int status_code;        /* Final HTTP status code. */
    http_method_t method;   /* HTTP method of request. */
    uint64_t start_time;    /* For latency logging. */

    /* The Decoded Request. */
    union {
        http1_req_t h1;     /* A parsed HTTP/1.1 request. */
        h2_stream_t h2;     /* A parsed HTTP/2 request. */
    } request;

    /* The Response to be built. */
    struct {
        kv_t headers[64];
        int header_count;
        char header_buffer[HEADER_BUFFER_SIZE];
        size_t body_len;
        void *body_data;
        bool is_mmap;
    } response;

    /* Routing/handler func. */
    void (*handler)(request_ctx_t *ctx);
};


struct conn_t {
    int fd;                /* Socket file descriptor. */
    uint8_t is_tls;        /* 0: http; 1: https */
    char remote_ip[46];    /* IPv4 or IPv6 address string. */
    uint16_t remote_port;  /* Client port. */
    SSL *ssl;              /* NULL for plain connections. */
    protocol_t protocol;   /* HTTP/1.1 HTTP/2 or Unknown. */
};


#endif //CELERITAS_TYPES_H
