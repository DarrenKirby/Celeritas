/*
 * 'src/threadpool.c'
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

#include "threadpool.h"

#include <stdlib.h>


Conn_Queue_t* initialize_connection_queue(void)
{
    Conn_Queue_t* cq = malloc(sizeof(Conn_Queue_t));
    cq->capacity = conf_data.conn_queue_size;
    pthread_mutex_init(&cq->mutex, nullptr);
   return cq;
}
