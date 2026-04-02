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

#include <stdint.h>
#include <pthread.h>
#include <openssl/types.h>

#define LOG_LINE_MAX  2048
#define HEADER_BUFFER_SIZE 8192
#define CACHE_LINE 64


typedef struct log_entry_t log_entry_t;
typedef struct log_ring_t log_ring_t;
typedef struct logger_t logger_t;
typedef struct conn_t conn_t;
typedef struct request_ctx_t request_ctx_t;
typedef struct kv_t kv_t;
typedef struct http1_req_t http1_req_t;
typedef struct h2_stream_t h2_stream_t;
typedef struct server_t server_t;
typedef struct config_data config_data;
typedef struct io_watcher_t io_watcher_t;
typedef struct io_event_t io_event_t;
typedef struct conn_pool_t conn_pool_t;

extern server_t server;
extern pthread_rwlock_t config_lock;
extern config_data *conf_data;
extern _Atomic int shutting_down;


typedef enum {
    /* Informational */
    SC_100_CONTINUE = 100,
    SC_101_SWITCHING_PROTOCOLS = 101,
    SC_102_PROCESSING = 102,
    SC_103_EARLY_HINTS = 103,

    /* Successful */
    SC_200_OK = 200,
    SC_201_CREATED = 201,
    SC_202_ACCEPTED = 202,
    SC_203_NON_AUTHORITATIVE_INFORMATION = 203,
    SC_204_NO_CONTENT = 204,
    SC_205_RESET_CONTENT = 205,
    SC_206_PARTIAL_CONTENT = 206,
    SC_207_MULTI_STATUS = 207,
    SC_208_ALREADY_REPORTED = 208,
    SC_226_IM_USED = 226,

    /* Redirection */
    SC_300_MULTIPLE_CHOICES = 300,
    SC_301_MOVED_PERMANENTLY = 301,
    SC_302_FOUND = 302,
    SC_303_SEE_OTHER = 303,
    SC_304_NOT_MODIFIED = 304,
    SC_305_USE_PROXY = 305,
    SC_306_UNUSED = 306,
    SC_307_TEMPORARY_REDIRECT = 307,
    SC_308_PERMANENT_REDIRECT = 308,

    /* Client Error */
    SC_400_BAD_REQUEST = 400,
    SC_401_UNAUTHORIZED = 401,
    SC_402_PAYMENT_REQUIRED = 402,
    SC_403_FORBIDDEN = 403,
    SC_404_NOT_FOUND = 404,
    SC_405_METHOD_NOT_ALLOWED = 405,
    SC_406_NOT_ACCEPTABLE = 406,
    SC_407_PROXY_AUTHENTICATION_REQUIRED = 407,
    SC_408_REQUEST_TIMEOUT = 408,
    SC_409_CONFLICT = 409,
    SC_410_GONE = 410,
    SC_411_LENGTH_REQUIRED = 411,
    SC_412_PRECONDITION_FAILED = 412,
    SC_413_CONTENT_TOO_LARGE = 413,
    SC_414_URI_TOO_LONG = 414,
    SC_415_UNSUPPORTED_MEDIA_TYPE = 415,
    SC_416_RANGE_NOT_SATISFIABLE = 416,
    SC_417_EXPECTATION_FAILED = 417,
    SC_418_IM_A_TEAPOT = 418,
    SC_421_MISDIRECTED_REQUEST = 421,
    SC_422_UNPROCESSABLE_CONTENT = 422,
    SC_423_LOCKED = 423,
    SC_424_FAILED_DEPENDENCY = 424,
    SC_425_TOO_EARLY = 425,
    SC_426_UPGRADE_REQUIRED = 426,
    SC_428_PRECONDITION_REQUIRED = 428,
    SC_429_TOO_MANY_REQUESTS = 429,
    SC_431_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    SC_451_UNAVAILABLE_FOR_LEGAL_REASONS = 451,

    /* Server Error */
    SC_500_INTERNAL_SERVER_ERROR = 500,
    SC_501_NOT_IMPLEMENTED = 501,
    SC_502_BAD_GATEWAY = 502,
    SC_503_SERVICE_UNAVAILABLE = 503,
    SC_504_GATEWAY_TIMEOUT = 504,
    SC_505_HTTP_VERSION_NOT_SUPPORTED = 505,
    SC_506_VARIANT_ALSO_NEGOTIATES = 506,
    SC_507_INSUFFICIENT_STORAGE = 507,
    SC_508_LOOP_DETECTED = 508,
    SC_510_NOT_EXTENDED = 510,
    SC_511_NETWORK_AUTHENTICATION_REQUIRED = 511

} http_status_code_t;


typedef enum {
    PROTO_UNKNOWN,
    PROTO_HTTP10,
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
    M_GET     = 1 << 0,
    M_HEAD    = 1 << 1,
    M_POST    = 1 << 2,
    M_PUT     = 1 << 3,
    M_DELETE  = 1 << 4,
    M_CONNECT = 1 << 5,
    M_OPTIONS = 1 << 6,
    M_TRACE   = 1 << 7,
    M_INVALID = 1 << 8
} http_method_t;


struct io_watcher_t {
    int fd;
};


struct io_event_t {
    void *userdata;
    int   is_error;
} ;


struct conn_pool_t {
    conn_t *items;
    size_t count;    /* Using count for unsigned length. */
    size_t capacity; /* The user-configured max. */
};


struct log_entry_t {
    log_target_t target;
    size_t       len;
    char         line[LOG_LINE_MAX];
};


struct log_ring_t {
    alignas(CACHE_LINE) int tail;                /* producers write to here. */
    alignas(CACHE_LINE) int head;                /* logger thread reads from here. */
    alignas(CACHE_LINE) pthread_mutex_t mutex;
    pthread_cond_t  not_empty;                   /* logger sleeps on this. */
    pthread_cond_t  not_full;                    /* producers sleep on this if full. */
    alignas(CACHE_LINE) log_entry_t *entries;
};


struct logger_t {
    int        access_fd;   /* Access log file descriptor. */
    int        event_fd;    /* Error log file descriptor. */
    int        server_pid;  /* The pid of the daemonized process. */
    uint16_t   ring_size;   /* The size of the log ring buffer. */
    log_ring_t *ring;       /* The log entry ring buffer. */
    pthread_t  thread;      /* The singleton logger thread ID. */
    int        shutdown;    /* Set to 1 by main thread to signal exit */
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
    int fd;                    /* Socket file descriptor. */
    uint8_t is_tls;            /* 0: http; 1: https */
    char remote_ip[46];        /* IPv4 or IPv6 address string. */
    uint16_t remote_port;      /* Client port. */
    SSL *ssl;                  /* NULL for plain connections. */
    protocol_t protocol;       /* HTTP/1.1 HTTP/2 or Unknown. */
    time_t keepalive_deadline; /* An absolute value denoting when the connection times out. */
};


#endif //CELERITAS_TYPES_H
