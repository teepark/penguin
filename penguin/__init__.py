"""bindings to linux syscalls missing from the standard library

this module contains python wrapper functions for system calls present
in recent versions of linux, and some other functions to support them.

corresponding directly to system calls (for the best explanation of what
these do, see the section 2 manpages, and see the individual docstrings
for how the C functions were mapped into python functions):

    - eventfd
    - signalfd
    - timerfd_create
    - timerfd_gettime
    - timerfd_settime
    - sigprocmask
    - aio_read
    - aio_write
    - aio_fsync
    - aio_cancel
    - aio_error
    - aio_return

sigprocmask does not create or manipulate a special file descriptor like
the others, it is there because signalfd is useless without it.

these functions are present purely to make dealing with these file
descriptors easier:

    - read_eventfd
    - write_eventfd
    - read_signalfd
    - read_aiocb_buffer
"""

from penguin.fds import *
from penguin.signals import *
from penguin.posix_aio import *
from penguin.linux_kaio import *
