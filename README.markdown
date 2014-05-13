# Explanation
penguin is a catch-all repository for wrapping libc functions that
aren't exposed in python's standard library.

so far we have:

- ``penguin.fds``: eventfd, timerfd, signalfd and inotify related
    functions
- ``penguin.signals``: exposing signalfd necessitated availability
    of sigprocmask, so here it is
- ``penguin.posix_aio``: the POSIX async file IO api, implemented in
    libc with a thread pool
- ``penguin.linux_kaio``: the in-kernel async file IO implementation
    made available in linux
- ``penguin.sysv_ipc``: the old System V IPC API
- ``pengiun.posix_ipc``: the newer POSIX IPC API


# Author
Travis J Parker <travis.parker@gmail.com>
