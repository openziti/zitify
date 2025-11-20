# Zitify

## What is it?

Zitify is a script for Linux that wraps execution of your existing program and enables it to connect to
[Ziti Services](https://openziti.github.io/ziti/services/overview.html). It brings _app-embeded Ziti_ without any code changes.

## Try Zitify

Download the latest release and extract it into a directory on your `$PATH`

Acquire an enrollment token from [ZEDS](https://zeds.openziti.org)

Enroll (assume your file is `my_id.jwt`). Python is required to execute the enrollment command:

```console
zitify enroll -j my_id.jwt -i my_id.json
```

Zitify `curl`!

```console
zitify -i my_id.json curl http://httpbin.ziti/json
```

## Hosting example

Assume you have `host.json` ziti identity that has bind permission to `cool-service`.
You can bind your application to that service
like this:

```console
zitify -i host.json -b 5555:cool-service ncat -l 5555
```

`-b` option configured a binding override, in the above example application trying to bind to port 5555 
will actually bind to `cool-service` on the OpenZiti network.

If that service has an intercept address (like `tcp:cool.service.ziti:1111`), 
then you can connect to it with `zitify`-ed client and an identity(`client.json`) allowed to _dial_ your service.

```console
zitify -i client.json ncat cool.service.ziti 1111
```

## How it Works

`zitify` is a shell script that employs the `LD_PRELOAD` trick (refer to [`man 8 ld.so`](https://man7.org/linux/man-pages/man8/ld.so.8.html)) to override a handful of networking-related functions from the GNU C standard library (`glibc`), e.g.,  `getaddrinfo()`, `getnameinfo()`, and `connect()` for dynamic executables.

Statically-linked binaries, like Go programs, and programs that do not link to `libc.so` (check links with command `ldd EXECUTABLE`), will not work with this tool.

## Building Locally

See [BUILD.md](BUILD.md) for instructions on building zitify from source.
