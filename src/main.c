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
#include <openssl/opensslv.h>
#include <openssl/crypto.h>


config_data conf_data;
sigset_t sig_mask;
extern logger_t logger;


int main(const int argc, char** argv)
{
    (void)argc;

    printf("Celeritas v0.0.1\n");
    printf("OpenSSL version (compile-time): %s\n", OPENSSL_VERSION_TEXT);
    printf("OpenSSL version (runtime):      %s\n", OpenSSL_version(OPENSSL_VERSION));
    printf("OpenSSL built on:               %s\n", OpenSSL_version(OPENSSL_BUILT_ON));
    printf("OpenSSL platform:               %s\n", OpenSSL_version(OPENSSL_PLATFORM));

    /* Read config, and populate conf struct.
     * TODO: override conf file settings with CLI args, if provided. */
    conf_data = read_config();

    /* Get server name. */
    char *cmd;
    if ((cmd = strrchr(argv[0], '/')) == NULL) cmd = argv[0];
    else cmd++;

    /* Daemonize the process. After this point, standard FDs are gone,
     * must use logger to signal errors. */
    daemonize();

    /* Initialize logging thread. */
    logger_init();

    log_write(&logger.ring, LOG_TARGET_EVENT, "%s %s - server started\n",
        l_priority(L_INFO), l_format_datetime());

    /* Write a lockfile; Make sure only one copy of the daemon is running. */
    char lockfile[PATH_MAX];
    snprintf(lockfile, PATH_MAX, "/Users/darrenkirby/code/celeritas/run/%s.pid", cmd);

    if (already_running(lockfile, &logger)) {
        log_write(&logger.ring, LOG_TARGET_EVENT, "%s %s - server already running!\n",
            l_priority(L_ERROR), l_format_datetime());
        pthread_join(logger.thread, nullptr);
        exit(1);
    }

    /* Restore SIGHUP default and block all signals. */
    struct sigaction    sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGHUP, &sa, nullptr) < 0) {
        log_write(&logger.ring, LOG_TARGET_EVENT, "%s %s - sigaction failed\n",
            l_priority(L_ERROR), l_format_datetime());
        pthread_join(logger.thread, nullptr);
        exit(1);
    }

    sigfillset(&sig_mask);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, nullptr) != 0) {
        log_write(&logger.ring, LOG_TARGET_EVENT, "%s %s - pthread_sigmask failed\n",
            l_priority(L_ERROR), l_format_datetime());
        pthread_join(logger.thread, nullptr);
        exit(1);
    }

    /* Create a thread to handle SIGHUP and SIGTERM. */
    pthread_t tid;
    if (pthread_create(&tid, nullptr, thr_sig_handler, &logger) != 0) {
        log_write(&logger.ring, LOG_TARGET_EVENT, "%s %s - pthread_create failed\n",
            l_priority(L_ERROR), l_format_datetime());
        pthread_join(logger.thread, nullptr);
        exit(1);
    }

    /* Initialize worker thread pool. */

    /* Start listener threads. */
    //pthread_t thr_http, thr_https;

    /* Block here until thr_sig_handler returns. */
    pthread_join(tid, nullptr);
    /* Delete the lockfile. */

    unlink(lockfile);
    printf("falling off main....");
    return 99;
}
