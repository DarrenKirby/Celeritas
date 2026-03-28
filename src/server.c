/*
 * 'src/server.c'
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

#include "server.h"
#include "logger.h"
#include "threadpool.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>

#define LOCK_MODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)


extern server_t server;


void daemonize(void)
{
    /* Clear file creation mask. */
    umask(0);

    /* Get maximum number of file descriptors. */
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        fprintf(stderr, "getrlimit: %s\n", strerror(errno));
        exit(1);
    }

    /* Become session leader to lose controlling TTY. */
    pid_t pid;
    if ((pid = fork()) < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        exit(1);
    }
    if (pid != 0) { exit(0); } /* Terminate parent. */
    setsid();

    if ((pid = fork()) < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        exit(1);
    }
    if (pid != 0) { exit(0); } /* Terminate parent. */

    /* Change CWD to root so we won't stop filesystems
     * from being unmounted. */
    if (chdir("/") < 0) {
        fprintf(stderr, "chdir: %s\n", strerror(errno));
        exit(1);
    }

    /* Close all open file descriptors. */
    if (rl.rlim_max == RLIM_INFINITY) { rl.rlim_max = 1024; }
    for (rlim_t i = 0; i < rl.rlim_max; i++) { close((int)i); }

    /* Attach file descriptors 0, 1, and 2 to /dev/null. */
    const int fd0 = open("/dev/null", O_RDWR);
    const int fd1 = dup(0);
    const int fd2 = dup(0);

    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        /* Nowhere to write an error to! */
        exit(1);
    }
}


int already_running(char* lockfile_name, logger_t* log)
{
    const int fd = open(lockfile_name, O_RDWR | O_CREAT, LOCK_MODE);

    if (fd < 0) {
        log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - can't open lockfile %s: %s\n",
                l_priority(L_ERROR), l_format_datetime(), lockfile_name, strerror(errno));
        server_shutdown(log, 1);
    }

    if (lockfile(fd) < 0) {
        if (errno == EACCES || errno == EAGAIN) {
            close(fd);
            return 1;
        }
        log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - can't lock lockfile %s: %s\n",
                l_priority(L_ERROR), l_format_datetime(), lockfile_name, strerror(errno));
        server_shutdown(log, 1);
    }

    ftruncate(fd, 0);
    char buf[16];
    snprintf(buf, sizeof(buf),"%ld", (long)getpid());
    write(fd, buf, strlen(buf) + 1);
    return 0;
}


void* thr_sig_handler(void *arg)
{
    const sig_handler_t* sig_handler = arg;
    logger_t* log = sig_handler->logger;
    const sigset_t* sig_mask = sig_handler->sig_mask;

    int sig_no;

    while (1) {
        const int ret = sigwait(sig_mask, &sig_no);
        if (ret != 0) {
            log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - can't wait for signal: %s\n",
                l_priority(L_ERROR), l_format_datetime(), strerror(ret));
            server_shutdown(log, 1);
        }

        switch (sig_no) {
            case SIGHUP:
                l_info(log, "server received SIGHUP");
                reread_config(log);
                break;
            case SIGTERM:
                l_info(log, "server received SIGTERM");
                server_shutdown(log, 0);
            default:
                log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - unexpected signal: %d\n",
                    l_priority(L_WARN), l_format_datetime());
        }
    }
}


void reread_config(logger_t* log)
{
    l_info(log, "Re-reading configuration file");

    pthread_mutex_lock(&conf_data.mutex);
    conf_data = read_config();
    pthread_mutex_unlock(&conf_data.mutex);
}


int lockfile(const int fd)
{
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    return fcntl(fd, F_SETLK, &fl);
}


void server_shutdown(logger_t* log, const int status)
{
    l_info(log, "server shutdown starting");

    shutting_down = 1;

    /* First thing to do is stop the listener threads. */
    close(server.http_fd);
    close(server.https_fd);
    pthread_join(server.http_listener, nullptr);
    pthread_join(server.https_listener, nullptr);

    /* Wake all workers. */
    pthread_mutex_lock(&server.queue->mutex);
    server.queue->shutting_down = 1;
    pthread_cond_broadcast(&server.queue->not_empty);
    pthread_mutex_unlock(&server.queue->mutex);

    /* Shut down worker thread pool. */
    for (int i = 0; i < server.n_workers; i++) {
        pthread_join(server.workers[i], nullptr);
    }

    l_info(log, "server shutdown complete");

    /* Shut down logging thread. */
    logger_shutdown(log);
    /* Remove the runtime lockfile. */
    unlink(conf_data.lock_file);
    exit(status);
}
