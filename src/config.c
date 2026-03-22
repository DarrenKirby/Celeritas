/*
 * 'src/config.c'
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

#include "config.h"


config_data read_config(void)
{
    config_data cd;
    cd.conn_queue_size = 8;
    cd.http_port = 80;
    cd.https_port = 443;
    cd.access_log = "/var/log/httpd/access_log";
    cd.event_log = "/var/log/httpd/event_log";
    const pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    cd.mutex = mtx;
    return cd;
}