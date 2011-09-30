"""bindings to linux 2.6+ eventfs facilities

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

sigprocmask does not create or manipulate a special file descriptor like
the others, it is there because signalfd is useless without it.

these functions are present purely to make dealing with these file
descriptors easier:

    - read_eventfd
    - write_eventfd
    - read_signalfd
"""

from _eventfs import *
