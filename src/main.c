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

#include <unistd.h>
#include <string.h>
#include <signal.h>
//#include <openssl/opensslv.h>
#include <openssl/crypto.h>


config_data *conf_data;
pthread_rwlock_t config_lock;
_Atomic int shutting_down = 0;


int main(const int argc, char** argv)
{
    printf("Waking up and putting the coffee on...\n");
    // printf("OpenSSL version (compile-time): %s\n", OPENSSL_VERSION_TEXT);
    // printf("OpenSSL version (runtime):      %s\n", OpenSSL_version(OPENSSL_VERSION));
    // printf("OpenSSL built on:               %s\n", OpenSSL_version(OPENSSL_BUILT_ON));
    // printf("OpenSSL platform:               %s\n", OpenSSL_version(OPENSSL_PLATFORM));


    /* Read config, populate conf struct. */
    resolve_config_path(argc, argv);
    init_config();

    /* Get server name. */
    char *cmd;
    if ((cmd = strrchr(argv[0], '/')) == NULL) cmd = argv[0];
    else cmd++;

    /* Daemonize the process. After this point, standard FDs are gone,
     * must use logger (or early_fatal) to signal errors. */
    daemonize();

    /* Need this to run before creating threads. */
    signal(SIGPIPE, SIG_IGN);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);

    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    /* Initialize logging thread. */
    logger_t* logger = logger_init();

    /* Grab the server pid. */
    logger->server_pid = getpid();

    l_info(logger, "server started");
    l_debug(logger, "initialized logger thread");

    /* Write a lockfile; Make sure only one copy of the daemon is running. */
    char lockfile[PATH_MAX];
    snprintf(lockfile, PATH_MAX, "%s/%s.pid", conf_data->lock_file_path, cmd);

    if (already_running(lockfile, logger)) {
        l_error(logger, "server already running");
        server_shutdown(logger, 1);
    }

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
    worker_init(logger, lockfile);

    /* Park main here. The signal handler thread drives all shutdown. */
    pthread_join(tid, nullptr);
}
