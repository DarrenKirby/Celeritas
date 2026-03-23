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


extern sigset_t sig_mask;


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

    /* Ensure future opens won’t allocate controlling TTYs. */
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGHUP, &sa, nullptr) < 0) {
        fprintf(stderr, "sigaction: %s\n", strerror(errno));
        exit(1);
    }

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
        pthread_join(log->thread, nullptr);
        exit(1);
    }

    if (lockfile(fd) < 0) {
        if (errno == EACCES || errno == EAGAIN) {
            close(fd);
            return 1;
        }
        log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - can't lock lockfile %s: %s\n",
                l_priority(L_ERROR), l_format_datetime(), lockfile_name, strerror(errno));
        pthread_join(log->thread, nullptr);
        exit(1);
    }

    ftruncate(fd, 0);
    char buf[16];
    snprintf(buf, sizeof(buf),"%ld", (long)getpid());
    write(fd, buf, strlen(buf) + 1);
    return 0;
}


void* thr_sig_handler(void *arg)
{
    logger_t* log = arg;
    int sig_no;
    int shutdown = 0;

    while (!shutdown) {
        if (sigwait(&sig_mask, &sig_no) != 0) {
            log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - can't wait for signal: %s\n",
                l_priority(L_ERROR), l_format_datetime(), strerror(errno));
            pthread_join(log->thread, nullptr);
            exit(1);
        }

        switch (sig_no) {
            case SIGHUP:
                log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - Re-reading configuration file\n",
                    l_priority(L_INFO), l_format_datetime());
                reread_config();
                break;
            case SIGTERM:
                log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - received SIGTERM, beginning shutdown\n",
                    l_priority(L_INFO), l_format_datetime());
                server_shutdown(log);
                shutdown = 1;
                break;
            default:
                log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - unexpected signal: %d\n",
                    l_priority(L_WARN), l_format_datetime());
        }
    }
    return nullptr;
}


void reread_config()
{
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


void server_shutdown(logger_t* log)
{
    log_write(&log->ring, LOG_TARGET_EVENT, "%s %s - server shutting down\n",
        l_priority(L_INFO), l_format_datetime());
    /* First thing to do is stop the listener threads. */

    /* Shut down worker thread pool. */

    /* Shut down logging thread. */
    logger_shutdown(log);
}
