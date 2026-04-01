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


#include "util.h"
#include "config.h"
#include "types.h"
#include "logger.h"
#include "threadpool.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <ctype.h>


extern char active_config_path[];


typedef enum {
    CFG_UINT16,
    CFG_STRING,
    CFG_BOOL
} cfg_type_t;


typedef struct {
    const char *key;
    cfg_type_t type;
    size_t offset;
    size_t max_len; /* Only used for CFG_STRING to prevent buffer overflows. */
} config_mapping_t;


/* The authoritative map of configuration keys to struct fields. */
static const config_mapping_t cfg_map[] = {
    {"http_port",          CFG_UINT16, offsetof(config_data, http_port), 0},
    {"https_port",         CFG_UINT16, offsetof(config_data, https_port), 0},
    {"worker_threads",     CFG_UINT16, offsetof(config_data, worker_threads), 0},
    {"max_url_size",       CFG_UINT16, offsetof(config_data, max_url_size), 0},
    {"max_rx_header_size", CFG_UINT16, offsetof(config_data, max_rx_header), 0},
    {"max_tx_header_size", CFG_UINT16, offsetof(config_data, max_tx_header), 0},
    {"dir_listing",        CFG_BOOL,   offsetof(config_data, dir_listing), 0},
    {"access_log",         CFG_STRING, offsetof(config_data, access_log), PATH_MAX},
    {"event_log",          CFG_STRING, offsetof(config_data, event_log), PATH_MAX},
    {"doc_root",           CFG_STRING, offsetof(config_data, doc_root), PATH_MAX},
    {"lock_file_path",     CFG_STRING, offsetof(config_data, lock_file_path), PATH_MAX},
    {"log_queue_size",     CFG_UINT16, offsetof(config_data, log_queue_size), 0},
    {"conn_queue_size",    CFG_UINT16, offsetof(config_data, conn_queue_size), 0},
};

#define CFG_MAP_SIZE (sizeof(cfg_map) / sizeof(cfg_map[0]))


static void handle_cfg_error(const logger_t* log, const bool is_first_run, const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (is_first_run) {
        fprintf(stderr, "Fatal Config Error: %s\n", buf);
        exit(EXIT_FAILURE);
    }
    l_error(log, "Config Error: %s. Using original value.", buf);
}


static char* trim_whitespace(char* str) {
    /* Trim leading space. */
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str; /* All spaces... */

    /* Trim trailing space. */
    char* end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}


bool parse_and_validate_config_file(const logger_t* log, config_data* conf, const bool is_first_run) {
    FILE* fp = fopen(active_config_path, "r");
    if (!fp) {
        handle_cfg_error(log, is_first_run, "Cannot open %s: %s",
                         active_config_path, strerror(errno));
        return false;
    }

    char line[1024];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Strip trailing newline. */
        line[strcspn(line, "\r\n")] = 0;

        char* trimmed_line = trim_whitespace(line);

        /* Ignore empty lines and comments. */
        if (trimmed_line[0] == '\0' || trimmed_line[0] == '#') {
            continue;
        }

        /* Find the '=' delimiter. */
        char* delim = strchr(trimmed_line, '=');
        if (!delim) {
            handle_cfg_error(log, is_first_run, "Line %d: Missing '=' delimiter", line_num);
            continue;
        }

        /* Split into key and value, and trim both. */
        *delim = '\0';
        char* key = trim_whitespace(trimmed_line);
        const char* value_str = trim_whitespace(delim + 1);

        /* Find the key in our mapping table. */
        bool key_found = false;
        for (size_t i = 0; i < CFG_MAP_SIZE; i++) {
            if (strcmp(key, cfg_map[i].key) == 0) {
                key_found = true;

                /* Calculate the exact memory address of the field inside the struct. */
                void* field_ptr = (char*)conf + cfg_map[i].offset;


                /* Parse based on the stored type. */
                switch (cfg_map[i].type) {
                    case CFG_STRING:
                        if (strlen(value_str) >= cfg_map[i].max_len) {
                            handle_cfg_error(log, is_first_run, "Line %d: String too long for '%s'", line_num, key);
                        } else {
                            strncpy((char*)field_ptr, value_str, cfg_map[i].max_len);
                        }
                        break;

                    case CFG_UINT16: {
                        char* endptr;
                        errno = 0;
                        const long val = strtol(value_str, &endptr, 10);
                        if (errno != 0 || *endptr != '\0' || val < 0 || val > UINT16_MAX) {
                            handle_cfg_error(log, is_first_run, "Line %d: Invalid uint16_t for '%s'", line_num, key);
                        } else {
                            *(uint16_t*)field_ptr = (uint16_t)val;
                        }
                        break;
                    }

                    case CFG_BOOL:
                        if (strcasecmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0) {
                            *(bool*)field_ptr = true;
                        } else if (strcasecmp(value_str, "false") == 0 || strcmp(value_str, "0") == 0) {
                            *(bool*)field_ptr = false;
                        } else {
                            handle_cfg_error(log, is_first_run, "Line %d: Invalid boolean for '%s'", line_num, key);
                        }
                        break;
                }
                break; /* Found and processed the key. */
            }
        }

        if (!key_found) {
            /* As with the rest of the config parsing, log and exit if first run, otherwise be forgiving. */
            handle_cfg_error(log, is_first_run, "Line %d: Unknown configuration key '%s'", line_num, key);
        }
    }
    fclose(fp);

    /* Post-processing and custom validation. */

    /* Bump queue sizes to next power of 2. */
    conf->log_queue_size = next_power_of_2(conf->log_queue_size);
    conf->conn_queue_size = next_power_of_2(conf->conn_queue_size);
    /* Remove trailing slash from run file dir if present. */
    const size_t path_len = strlen(conf->lock_file_path);
    if (conf->lock_file_path[path_len - 1] != '/') {
        conf->lock_file_path[path_len - 1] = '\0';
    }

    return true;
}


void load_config_defaults(config_data *cd)
{
    const long ncpu = get_ncpu();

    /* Start with some defaults that can be overridden by
     * values in the config file, or passed on the CLI. */

    cd->http_port = 80;
    cd->https_port = 443;
    cd->worker_threads = ncpu * 2;
    cd->conn_queue_size = 256;
    cd->log_queue_size = 256;
    strncpy(cd->access_log, "/var/log/celeritas/logs/access_log", PATH_MAX);
    strncpy(cd->event_log, "/var/log/celeritas/logs/event_log", PATH_MAX);
    strncpy(cd->doc_root, "/var/www/html", PATH_MAX);
    strncpy(cd->lock_file_path, "/var/run/", PATH_MAX);
    strncpy(cd->server_tok, "Celeritas/0.10.2", 20);
    cd->max_rx_header = 8192;
    cd->max_tx_header = 8192;
    cd->max_url_size = 2048;
    cd->keepalive_timeout = 10;
}


void init_config(void) {
    /* Initialize the attribute object. */
    pthread_rwlockattr_t attr;
    if (pthread_rwlockattr_init(&attr) != 0) {
        early_fatal("Failed to initialize rwlock attributes");
    }

    /* Set the writer-preference attribute (Linux/glibc only). */
#if defined(__linux__)
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif

    /* Initialize the actual lock using the configured attributes. */
    if (pthread_rwlock_init(&config_lock, &attr) != 0) {
        early_fatal("Failed to initialize configuration rwlock");
    }

    /* Destroy the attribute object immediately. */
    pthread_rwlockattr_destroy(&attr);

    /* Allocate the initial configuration structure. */
    conf_data = calloc(1, sizeof(config_data));
    if (!conf_data) {
        early_fatal("Failed to allocate memory for configuration");
    }
    
    load_config_defaults(conf_data);
    parse_and_validate_config_file(nullptr, conf_data, true);
}


void cleanup_config(void) 
{
    pthread_rwlock_destroy(&config_lock);
    if (conf_data) {
        free(conf_data);
        conf_data = nullptr;
    }
}


void reload_configuration(const logger_t* log)
{
    /* Do the slow work entirely outside the lock. */
    config_data *new_config = malloc(sizeof(config_data));
    if (!new_config) {
        l_error(log, "Malloc failed parsing new config!");
        return;
    }
    load_config_defaults(new_config);
    if (!parse_and_validate_config_file(log, new_config, false)) {
        free(new_config);
        return; /* Parsing failed, keep the old config. */
    }

    /* ### Critical section start. ### */
    THR_OK(pthread_rwlock_wrlock(&config_lock));
    config_data *old_config = conf_data;
    conf_data = new_config;
    THR_OK(pthread_rwlock_unlock(&config_lock));
    /* ### Critical section end. ### */

    free(old_config);
}


/* Example config file format including default values
 * -------------------------------------------------- *

# Ports
http_port = 80
https_port = 443

# Threads
worker_threads = 12

# Paths
access_log = /var/log/celeritas/access_log
event_log = /var/log/celeritas/event_log
lock_file_path = /var/run/
doc_root = /var/www/html/

# Limits
max_rx_header_size = 8192
max_tx_header_size = 8192
max_url_size = 2048
keepalive_timeout = 10

# Queue sizes (will be rounded up to the next power of 2)
log_queue_size = 512
connection_queue_size = 512

 * ----------------------------------------------------- */
