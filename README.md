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
export ZITI_IDENTITIES=my_id.json
zitify curl http://httpbin.ziti/json
```

## How it Works

`zitify` is a shell script that employs the `LD_PRELOAD` trick (refer to `man 8 ld.so`) to override a handful of networking-related functions from the GNU C standard library (`glibc`), e.g.,  `getaddrinfo()`, `getnameinfo()`, and `connect()` for dynamic executables.

Statically-linked binaries, like Go programs, will not pre-load `libzitify.so`.
