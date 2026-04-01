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
#include "types.h"
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
#include <getopt.h>

#define DEFAULT_CONFIG_PATH "/etc/celeritas/celeritas.conf"
#define LOCK_MODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define APPNAME "celeritas"
#define APPVERSION "0.10.2"


char active_config_path[PATH_MAX];


static void show_help(void) {
    printf("Usage: %s [OPTION] | [-c | --config-file=PATH] \n\n\
Options:\n\
    -c, --config-file\t\tpath to the celeritas configuration file\n\
    -h, --help\t\tdisplay this help and exit\n\
    -V, --version\tdisplay version information and exit\n\n\
Report bugs to <darren@dragonbyte.ca>\n", APPNAME);
}


void resolve_config_path(int argc, char *argv[])
{
    int opt;
    const char *cli_path = nullptr;

    const struct option long_opts[] = {
        {"help", 0, nullptr, 'h'},
        {"version", 0, nullptr, 'V'},
        {"config-file", 1, nullptr, 'c'},
        {nullptr,0,nullptr,0}
    };

    while ((opt = getopt_long(argc, argv, "Vhc:", long_opts, nullptr)) != -1) {
        switch(opt) {
            case 'V':
                printf("%s version %s\n", APPNAME, APPVERSION);
                printf("compiled on %s at %s\n", __DATE__, __TIME__);
                exit(EXIT_SUCCESS);
            case 'h':
                show_help();
                exit(EXIT_SUCCESS);
            case 'c':
                cli_path = optarg;
                break;
            default:
                show_help();
                exit(EXIT_FAILURE);
        }
    }

    /* Apply hierarchy of precedence. */
    if (cli_path != NULL) {
        strncpy(active_config_path, cli_path, sizeof(active_config_path) - 1);
    } else {
        const char *env_path = getenv("CELERITAS_CONF");
        if (env_path != NULL) {
            strncpy(active_config_path, env_path, sizeof(active_config_path) - 1);
        } else {
            strncpy(active_config_path, DEFAULT_CONFIG_PATH, sizeof(active_config_path) - 1);
        }
    }

    active_config_path[sizeof(active_config_path) - 1] = '\0';
}


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
    /* FIXME: disabled cd during testing, so I can cheat and use
     * relative paths for files. Restore this after proper local
     * config file parsing has been implemented. */
    // if (chdir("/") < 0) {
    //     fprintf(stderr, "chdir: %s\n", strerror(errno));
    //     exit(1);
    // }

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
        l_error(log, "can't open lockfile %s: %s", lockfile_name, strerror(errno));
        server_shutdown(log, 1);
    }

    if (lockfile(fd) < 0) {
        if (errno == EACCES || errno == EAGAIN) {
            close(fd);
            return 1;
        }
        l_error(log, "can't lock lockfile %s: %s", lockfile_name, strerror(errno));
        server_shutdown(log, 1);
    }

    if (ftruncate(fd, 0) < 0) {
        l_error(log, "can't truncate lockfile %s: %s", lockfile_name, strerror(errno));
    }
    char buf[16];
    const ssize_t bytes_in_buf = snprintf(buf, sizeof(buf),"%ld", (long)getpid());
    const ssize_t rv = write(fd, buf, bytes_in_buf);
    if (rv < 0) {
        l_warn(log, "can't write to lockfile %s: %s", lockfile_name, strerror(errno));
    }
    return 0;
}


// ReSharper disable once CppParameterMayBeConstPtrOrRef
void* thr_sig_handler(void *arg)
{
    const sig_handler_t* sig_handler = arg;
    logger_t* log = sig_handler->logger;
    const sigset_t* sig_mask = sig_handler->sig_mask;

    int sig_no;

    while (1) {
        const int ret = sigwait(sig_mask, &sig_no);
        if (ret != 0) {
            l_error(log, "sigwait failed: %s", strerror(ret));
            server_shutdown(log, 1);
        }

        switch (sig_no) {
            case SIGHUP:
                l_info(log, "server received SIGHUP");
                reload_configuration(log);
                break;
            case SIGTERM:
                l_info(log, "server received SIGTERM");
                server_shutdown(log, 0);
            default:
                l_error(log, "unexpected signal: %d", sig_no);
        }
    }
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
    THR_OK(pthread_join(server.http_listener, nullptr));
    THR_OK(pthread_join(server.https_listener, nullptr));

    /* Wake the wait room thread. */
    THR_OK(pthread_mutex_lock(&server.wait_queue->mutex));
    server.wait_queue->shutting_down = 1;
    THR_OK(pthread_cond_signal(&server.wait_queue->not_empty));
    THR_OK(pthread_mutex_unlock(&server.wait_queue->mutex));
    THR_OK(pthread_join(server.wait_room_thread, nullptr));

    /* Wake all workers. */
    THR_OK(pthread_mutex_lock(&server.work_queue->mutex));
    server.work_queue->shutting_down = 1;
    THR_OK(pthread_cond_broadcast(&server.work_queue->not_empty));
    THR_OK(pthread_mutex_unlock(&server.work_queue->mutex));

    /* Shut down worker thread pool. */
    for (int i = 0; i < server.n_workers; i++) {
        pthread_join(server.workers[i], nullptr);
    }

    free(server.work_queue);
    free(server.wait_queue);
    free(server.workers);

    l_info(log, "server shutdown complete");

    /* Shut down logging thread. */
    logger_shutdown(log);
    /* Remove the runtime lockfile. */
    unlink(server.lock_file);
    /* Clean up config. */
    cleanup_config();
    /* See ya... */
    exit(status);
}
