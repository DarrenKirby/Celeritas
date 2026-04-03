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
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <poll.h>


extern SSL_CTX *ssl_ctx;


/* Performs the TLS handshake to initialize
 * TLS/SSL connections. */
int perform_tls_handshake(const conn_t *conn)
{
    while (1) {
        const int ret = SSL_accept(conn->ssl);

        /* Handshake complete. */
        if (ret == 1) {
            return 0;
        }

        const int err = SSL_get_error(conn->ssl, ret);

        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            /* The non-blocking socket is starving. Sleep until the client
             * sends the next piece of the cryptographic handshake. */
            struct pollfd pfd;
            pfd.fd = conn->fd;

            /* Listen for whatever OpenSSL told us it needs. */
            pfd.events = (err == SSL_ERROR_WANT_READ) ? POLLIN : POLLOUT;

            /* 5000ms timeout to prevent Slowloris handshake attacks. */
            int p_res = poll(&pfd, 1, 5000);

            if (p_res == 0) return -1; /* Timeout: client took too long. */
            if (p_res < 0) {
                if (errno == EINTR) continue; /* Interrupted, try again. */
                return -1; /* Actual OS poll error. */
            }

            /* Loop around and let SSL_accept try again. */
            continue;
        }

        /* If here, it's a fatal TLS protocol error. */
        return -1;
    }
}


/* Calls socket(), bind(), and listen() on the specified port. */
int create_listener(const uint16_t port)
{
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints = {0}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;        /* IPv4 or IPv6. */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;        /* For bind() */

    const int rv = getaddrinfo(nullptr, port_str, &hints, &res);
    if (rv != 0) {
        fprintf(stderr, "getaddrinfo() failed: %s", gai_strerror(rv));
        return -1;
    }

    int listen_fd = -1;

    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }

        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        /* Allow IPv4 on IPv6 sockets (Linux-specific behaviour). */
        if (p->ai_family == AF_INET6) {
            int no = 0;
            setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
        }

        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == 0) {
            /* Success. */
            break;
        }

        fprintf(stderr, "bind() failed: %s", strerror(errno));
        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(res);

    if (listen_fd < 0) {
        fprintf(stderr, "failed to bind to port %u", port);
        return -1;
    }

    if (listen(listen_fd, 128) != 0) {
        fprintf(stderr, "listen() failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}


/* Used by the listener threads to accept connections on
 * their assigned ports. */
int accept_connection(const logger_t *log, const int listen_fd, const int is_tls, conn_t* conn) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    const int fd = accept(listen_fd, (struct sockaddr*)&addr, &len);
    if (fd < 0) {
        /* Suppress logging error if shutting down. */
        if (shutting_down) return 0;
        l_error(log, "accept() failed: %s", strerror(errno));
        return fd;
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

    if (conn->is_tls) {
        /* Initialize SSL struct. */
        conn->ssl = SSL_new(ssl_ctx);
        if (conn->ssl == nullptr) {
            l_error(log, "SSL_new() failed");
            return -1;
        }

        /* Bind the raw socket to the SSL object. */
        if (SSL_set_fd(conn->ssl, conn->fd) != 1) {
            l_error(log, "SSL_set_fd() failed");
            return -1;
        }
    } else {
        conn->ssl = nullptr;
    }

    /* Set to unknown on new connection. */
    conn->protocol = PROTO_UNKNOWN;
    return 0;
}


/* Attempts to sniff out an HTTP/2 connection preface,
 * else defaults the protocol to HTTP/1.1. */
void demux_protocol(conn_t* conn)
{
    char peek_buf[24];
    const ssize_t n = recv(conn->fd, peek_buf, sizeof(peek_buf), MSG_PEEK);
    if (n == 0) {
        /* Assume HTTP/1.1 and move on... */
        conn->protocol = PROTO_HTTP11;
        return;
    }
    if (n < 0) {
        conn->protocol = PROTO_UNKNOWN;
        return;
    }

    if (memcmp(peek_buf, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) == 0) {
        conn->protocol = PROTO_HTTP2;
        return;
    }
    conn->protocol = PROTO_HTTP11;
}


/* Sets a socket timeout on the passed file descriptor. */
void set_socket_timeout(const int fd, const int seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
}


/* Closes the socket and thus closes the passed connection. */
void close_connection(const conn_t* conn)
{
    close(conn->fd);
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }
}
