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
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <getopt.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define DEFAULT_CONFIG_PATH "/etc/celeritas/celeritas.conf"
#define LOCK_MODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define APPNAME "celeritas"
#define APPVERSION "0.10.2"


char active_config_path[PATH_MAX];


static void show_help(void) {
    printf("Usage: %s [OPTION] | [-c | --config-file=PATH] \n\n\
Options:\n\
    -c, --config-file\tpath to the celeritas configuration file\n\
    -v, --verbose\tbe more chatty on startup (does not affect logging)\n\
    -h, --help\t\tdisplay this help and exit\n\
    -V, --version\tdisplay version information and exit\n\n\
Report bugs to <darren@dragonbyte.ca>\n", APPNAME);
}


/* Collects CLI args, and applies the configuration file hierarchy:
 * 1. CLI argument (-c <path> or --config-file=<path>).
 * 2. Value of CELERITAS_CONF environmental variable.
 * 3. Fixed '/etc/celeritas/celeritas.conf
 */
void resolve_config_path(int argc, char *argv[])
{
    int opt;
    const char *cli_path = nullptr;

    const struct option long_opts[] = {
        {"help", 0, nullptr, 'h'},
        {"version", 0, nullptr, 'V'},
        {"verbose", 0, nullptr, 'v'},
        {"config-file", 1, nullptr, 'c'},
        {nullptr,0,nullptr,0}
    };

    while ((opt = getopt_long(argc, argv, "Vhvc:", long_opts, nullptr)) != -1) {
        switch(opt) {
            case 'V':
                printf("%s version %s\n", APPNAME, APPVERSION);
                printf("  compiled on %s at %s\n", __DATE__, __TIME__);
                exit(EXIT_SUCCESS);
            case 'h':
                show_help();
                exit(EXIT_SUCCESS);
            case 'c':
                cli_path = optarg;
                break;
            case 'v':
                printf("\n%s version %s: the 'caffeinated' http server\n", APPNAME, APPVERSION);
                printf("  report bugs to: <darren@dragonbyte.ca>\n");
                printf("           or at: https://github.com/DarrenKirby/Celeritas\n");
                printf("  compiled on %s at %s\n\n", __DATE__, __TIME__);
                printf("OpenSSL version (compile-time): %s\n", OPENSSL_VERSION_TEXT);
                printf("OpenSSL version (runtime):      %s\n", OpenSSL_version(OPENSSL_VERSION));
                printf("OpenSSL built on:               %s\n", OpenSSL_version(OPENSSL_BUILT_ON));
                printf("OpenSSL platform:               %s\n", OpenSSL_version(OPENSSL_PLATFORM));
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


/* Called once at startup. This function initializes the
 * SSL_CTX struct necessary for TLS/SSL connections. */
SSL_CTX *init_ssl_context(const char *cert_path, const char *key_path)
{
    /* Use TLS_server_method() for modern OpenSSL versions (1.1.0+). */
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);

    if (!ctx) {
        fprintf(stderr, "Unable to create SSL context\n");
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    /* Enforce modern security: disable old SSLv3, TLSv1.0, TLSv1.1. */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    /* Load the certificate */
    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Failed to load certificate: %s\n", cert_path);
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    /* Load the private key. */
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "Failed to load private key: %s\n", key_path);
        ERR_print_errors_fp(stderr);
        return nullptr;
    }

    /* Verify the key matches the certificate. */
    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match the public certificate\n");
        return nullptr;
    }

    return ctx;
}


/* If root, drop privileges to the user/group specified in the configuration file,
 * or www/www by default. */
int drop_privileges(const char *username, const char *groupname, const int fd)
{
    /* Look up the group and user records. */
    struct group *grp = getgrnam(groupname);
    if (!grp) {
        dprintf(fd, "Fatal: Group '%s' not found.\n", groupname);
        return -1;
    }

    struct passwd *pwd = getpwnam(username);
    if (!pwd) {
        dprintf(fd, "Fatal: User '%s' not found.\n", username);
        return -1;
    }

    /* Drop supplementary groups. */
    if (initgroups(username, (int)grp->gr_gid) != 0) {
        dprintf(fd, "Fatal: Failed to clear supplementary groups: %s\n", strerror(errno));
        return -1;
    }

    /* Drop the primary group (MUST be done before dropping the user). */
    if (setgid(grp->gr_gid) != 0) {
        dprintf(fd, "Fatal: Failed to drop group privileges: %s\n", strerror(errno));
        return -1;
    }

    /* Drop the primary user permanently. */
    if (setuid(pwd->pw_uid) != 0) {
        dprintf(fd, "Fatal: Failed to drop user privileges: %s\n", strerror(errno));
        return -1;
    }

    /* Security Check: Prove we can't get root back. */
    if (setuid(0) == 0) {
        dprintf(fd, "Fatal: Privilege drop failed. Successfully regained root!\n");
        return -1;
    }

    return 0; /* We are permanently unprivileged. */
}


/* Perform a double-fork to drop the controlling terminal
 * and daemonize the server. */
void daemonize(void)
{
    /* Clear file creation mask. */
    umask(0);

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

    /* Open /dev/null */
    const int dev_null = open("/dev/null", O_RDWR);
    if (dev_null < 0) {
        /* If we can't even open /dev/null, the system is fundamentally broken. */
        exit(1);
    }

    /* Atomically close the old 0,1,2 and redirect them to /dev/null */
    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);

    /* Close the original /dev/null FD if it's outside the standard 3 */
    if (dev_null > STDERR_FILENO) {
        close(dev_null);
    }
}

/* Attempt to write a runtime lock file to determine if the server
 * has already been started or not. */
int already_running(const char* lockfile_name, const int elfd)
{
    const int fd = open(lockfile_name, O_RDWR | O_CREAT, LOCK_MODE);

    if (fd < 0) {
        dprintf(elfd, "can't open lockfile %s: %s", lockfile_name, strerror(errno));
        return 1;
    }

    if (lockfile(fd) < 0) {
        if (errno == EACCES || errno == EAGAIN) {
            close(fd);
            return 1;
        }
        dprintf(elfd, "can't lock lockfile %s: %s", lockfile_name, strerror(errno));
        return 1;
    }

    if (ftruncate(fd, 0) < 0) {
        dprintf(elfd, "can't truncate lockfile %s: %s", lockfile_name, strerror(errno));
    }
    char buf[16];
    const ssize_t bytes_in_buf = snprintf(buf, sizeof(buf),"%ld", (long)getpid());
    const ssize_t rv = write(fd, buf, bytes_in_buf);
    if (rv < 0) {
        dprintf(fd, "can't write to lockfile %s: %s", lockfile_name, strerror(errno));
        return 1;
    }
    return 0;
}


/* A singleton thread that just listens for SIGHUP and SIGTERM,
 * in order to trigger configuration file re-reading, or server shutdown. */
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


/* Locks the runtime pid file. */
int lockfile(const int fd)
{
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    return fcntl(fd, F_SETLK, &fl);
}


/* Orchestrates the clean shutdown of the server. */
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

    /* Free the work/wait queues. */
    free(server.work_queue);
    free(server.wait_queue);
    free(server.workers);

    /* Remove the runtime lockfile. */
    unlink(server.lock_file);
    /* Clean up config. */
    cleanup_config();

    l_info(log, "server shutdown complete");

    /* Shut down logging thread. */
    logger_shutdown(log);

    /* See ya... */
    exit(status);
}
