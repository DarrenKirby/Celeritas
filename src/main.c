/*
 * 'src/main.c'
 * This file is part of Celeritas - https://github.com/DarrenKirby/celeritas
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
#include "server.h"
#include "threadpool.h"
#include "logger.h"
#include "util.h"
#include "socket.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>


config_data *conf_data;
pthread_rwlock_t config_lock;
_Atomic int shutting_down = 0;
SSL_CTX *ssl_ctx;


int main(const int argc, char** argv)
{
    printf("Waking up and putting the coffee on...\n");

    /* Read config, populate conf struct. */
    resolve_config_path(argc, argv);
    init_config();

    /* Get server name. */
    char *cmd;
    if ((cmd = strrchr(argv[0], '/')) == NULL) cmd = argv[0];
    else cmd++;

    /* Initialize TLS/SSL. */
    ssl_ctx = init_ssl_context(conf_data->tls_cert_path, conf_data->tls_key_path);
    if (ssl_ctx == nullptr) {
        fprintf(stderr, "failed to initialize SSL context...aborting\n");
        return 1;
    }

    /* Validate paths, and open the system logs.
     * Note there is no need to check status of
     * validate_path(), as all errors are fatal
     * and are logged to stderr within the call. */
    validate_path(conf_data->event_log);
    validate_path(conf_data->access_log);

    const int event_log_fd =  open(conf_data->event_log, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (event_log_fd < 0) {
        fprintf(stderr, "failed to open event log file: %s\n", strerror(errno));
        return 1;
    }

    const int access_log_fd = open(conf_data->access_log, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (access_log_fd < 0) {
        fprintf(stderr, "failed to open access_log file: %s\n", strerror(errno));
        return 1;
    }

    /* Call socket(), bind(), and listen() on the http and https ports. */
    const int http_sock = create_listener(conf_data->http_port);
    if (http_sock < 0) {
        fprintf(stderr, "failed to create listener on port %d\n", conf_data->http_port);
        return 1;
    }

    const int https_sock = create_listener(conf_data->https_port);
    if (https_sock < 0) {
        fprintf(stderr, "failed to create listener on port %d\n", conf_data->https_port);
        return 1;
    }

    /* Daemonize the process. After this point, standard FDs are gone,
     * must use dprintf to signal errors until logging subsystem is running. */
    daemonize();

    /* Write a lockfile; Make sure only one copy of the daemon is running. */
    char lockfile[PATH_MAX];
    const ssize_t bytes = snprintf(lockfile, PATH_MAX, "%s/%s.pid", conf_data->lock_file_path, cmd);
    if (bytes < 0 || bytes >= (int)sizeof(lockfile)) {
        fprintf(stderr, "lockfile path truncated\n");
        exit(EXIT_FAILURE);
    }

    if (already_running(lockfile, event_log_fd)) {
        dprintf(event_log_fd, "server already running\n");
        return 1;
    }

    /* Drop privileges, if necessary. */
    if (getuid() == 0) {
        const int ret = drop_privileges(conf_data->server_user, conf_data->server_group, event_log_fd);
        if (ret < 0) {
            dprintf(event_log_fd, "failed to drop privileges\n");
            return 1;
        }
    }

    /* Best that this runs before creating threads. */
    signal(SIGPIPE, SIG_IGN);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);

    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    /* Initialize logging thread. */
    logger_t* logger = logger_init(access_log_fd, event_log_fd);

    /* Grab the server pid. */
    logger->server_pid = getpid();

    l_info(logger, "server started");
    l_debug(logger, "initialized logger thread");

    static sig_handler_t sht;
    sht.logger = logger;
    sht.sig_mask = &set;

    /* Create a thread to handle SIGHUP and SIGTERM. */
    l_debug(logger, "creating signal handler thread");
    pthread_t tid;
    if (pthread_create(&tid, nullptr, thr_sig_handler, &sht) != 0) {
        l_error(logger, "pthread_create failed");
        server_shutdown(logger, 1);
    }

    /* Initialize worker thread pool, listener threads, and wait room thread. */
    l_debug(logger, "initializing worker thread pool");
    worker_init(logger, lockfile, http_sock, https_sock);

    /* Park main here. The signal handler thread drives all shutdown. */
    pthread_join(tid, nullptr);
}
