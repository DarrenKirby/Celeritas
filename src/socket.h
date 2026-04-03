/*
 * 'src/socket.h'
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


#ifndef CELERITAS_SOCKET_H
#define CELERITAS_SOCKET_H

#include "types.h"
#include "config.h"


int create_listener(uint16_t port);
int accept_connection(logger_t *log, int listen_fd, int is_tls, conn_t* conn);
void demux_protocol(conn_t* conn);
void set_socket_timeout(int fd, int seconds);
void close_connection(const conn_t* conn);
int perform_tls_handshake(const conn_t *conn);

#endif //CELERITAS_SOCKET_H
