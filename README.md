# Celeritas

**Celeritas** is a multithreaded HTTP server written in C. It spawns singleton threads
for logging I/O, and signal handling (SIGHUP and SIGTERM). It spawns two listener threads
which accept connections on the specified HTTP and HTTPS ports. It spawns a configurable
pool of worker threads that handle the connections. Finally, it spawns a singleton thread
that accepts keep-alive connections from the workers, and monitors them (using kqueue or 
epoll) until they either become read-ready or time out.

Currently, the server supports HTTP/1.0 and HTTP/1.1, using either plain text (HTTP) or 
TLS/SSL encrypted connections (HTTPS). Presently, it only supports the GET, HEAD, and OPTIONS
HTTP methods.

The server is written to C23 standards, and compiles cleanly using
`-Wall -Wextra -Werror`. The only dependency is OpenSSL. Sadly, it is not POSIX portable, 
as it uses the Linux-specific `epoll()` mechanism and the *BSD/macOS-specific `kqueue()`
mechanism to poll file ddescriptors for read-ready status. I have personally tested and can 
confirm it builds and works on Linux and macOS (FreeBSD testing coming soon).

## TODO

* Make more settings configurable
* More method handlers (POST etc...)
* HTTP/2
* HTTP/3 (maybe...)
* Dynamic content handlers
* Virtual hosting

## Building the Server

**Celeritas** uses CMake as the build system. After cloning the source, run:

``$ mkdir build && cd build && cmake ..``

and finally:

``$ make``

## Configuring the Server

The source repo includes a ``celeritas.conf.dist`` file which is well-commented and enumerates
the various valid "key = value" pairs. It also specifies the default values used by the server
internally. Only values the user wants to change need to be specified in the config file,
missing settings will be configured with the defaults.

On startup, the server will resolve a hierarchy of possible config file specifications. In order,
the server wil look for and use:

1. The value passed to the ``-c`` or ``--config-file`` CLI argument.
2. The value of the ``CELERITAS_CONF`` environmental variable.
3. The fixed location ``/etc/celeritas/celeritas.conf``

If the server cannot find a valid configuration file it will error and exit.

## Running the Server

Upon starting, the server will print a short message then detach from the controlling 
terminal to run in the background. If started as root (UID 0), it will open the log files,
bind to the specified ports, write a runtime PID file, and then drop privileges permanently
before accepting and processing connections.

Note that the server will fail on startup if it cannot find a valid SSL certificate and key.
If you do not have these files, you can generate them by running:

``$ openssl req -x509 -newkey rsa:2048 -nodes -keyout celeritas.key -out celeritas.crt -days 365 -subj "/CN=localhost"``

As these are self-signed, graphic browsers will complain and show a "this connection is
dangerous" message, which will have to be bypassed to continue and retrieve the content.
Likewise, testing with ``curl`` will require the ``-k`` or ``--insecure`` flag, for example:

``$ curl -k -I https://localhost:443/``

