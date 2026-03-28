/*
 * 'src/mime.c'
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

#include "mime.h"

#include <string.h>


typedef struct {
    const char *extension;
    const char *mime_type;
} mime_entry_t;


static const mime_entry_t mime_table[] = {
    {".html", "text/html"},
    {".htm",  "text/html"},
    {".css",  "text/css"},
    {".js",   "text/javascript"},
    {".txt",  "text/plain"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png",  "image/png"},
    {".gif",  "image/gif"},
    {".webp", "image/webp"},
    {".ico",  "image/x-icon"},
    {".svg",  "image/svg+xml"},
    {".woff2", "font/woff2"},
    {NULL, NULL}
};


const char* get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";

    for (int i = 0; mime_table[i].extension != NULL; i++) {
        if (strcasecmp(ext, mime_table[i].extension) == 0) {
            return mime_table[i].mime_type;
        }
    }

    return "application/octet-stream";
}
