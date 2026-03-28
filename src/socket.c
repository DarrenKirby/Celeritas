/*
 * 'src/socket.c'
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


#include "socket.h"
#include "logger.h"

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>


int create_listener(uint16_t port, logger_t *log)
{
    const int fd = socket(AF_INET6, SOCK_STREAM, 0);

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);

    const int ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret != 0) {
        log_write(&log->ring, LOG_TARGET_EVENT, "%s - %s - pid %d - tid %p - bind failed: %s\n",
                l_priority(L_ERROR), l_format_datetime(), conf_data.server_pid, get_tid(), strerror(errno));
        close(fd);
        return -1;
    }
    listen(fd, 128);

    return fd;
}


int accept_connection(const int listen_fd, const int is_tls, conn_t* conn) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    const int fd = accept(listen_fd, (struct sockaddr*)&addr, &len);
    if (fd < 0) {
        /* Suppress logging error if shutting down. */
        if (shutting_down) return 0;
        const int rv = errno;
        return rv;
    }

    conn->fd = fd;
    conn->is_tls = is_tls;

    /* Fill remote IP + port. */
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in*)&addr;
        inet_ntop(AF_INET, &a->sin_addr, conn->remote_ip, sizeof(conn->remote_ip));
        conn->remote_port = ntohs(a->sin_port);
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6*)&addr;
        inet_ntop(AF_INET6, &a->sin6_addr, conn->remote_ip, sizeof(conn->remote_ip));
        conn->remote_port = ntohs(a->sin6_port);
    }

    /* Just set to null for now, worker handles handshake. */
    conn->ssl = nullptr;
    /* Set to unknown on new connection. */
    conn->protocol = PROTO_UNKNOWN;
    return 0;
}


void demux_protocol(request_ctx_t* ctx)
{
    char peek_buf[24];
    const ssize_t n = recv(ctx->conn->fd, peek_buf, sizeof(peek_buf), MSG_PEEK|MSG_DONTWAIT);
    if (n == 0) {
        /* Assume HTTP/1.1 and move on... */
        ctx->conn->protocol = PROTO_HTTP11;
        return;
    }
    if (n < 0) {
        l_warn(ctx->log, "Initial read attempt from socket failed! Dropping connection");
        ctx->conn->protocol = PROTO_UNKNOWN;
        ctx->status_code = 500; /* Internal Server Error. */
        return;
    }

    if (memcmp(peek_buf, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) == 0) {
        ctx->conn->protocol = PROTO_HTTP2;
        return;
    }
    ctx->conn->protocol = PROTO_HTTP11;
}


void set_socket_timeout(const int fd, const int seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
}


void close_connection(const conn_t* conn)
{
    close(conn->fd);
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }
}
