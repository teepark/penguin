#!/usr/bin/env python
# vim: fileencoding=utf8:et:sta:ai:sw=4:ts=4:sts=4

import select
import os

from penguin import linux_kaio, fds


ctx = linux_kaio.io_setup(10)

fd = os.open("TODO", os.O_RDONLY)
evfd = fds.eventfd()

op = linux_kaio.io_prep_pread(fd, 8192, eventfd=evfd)
linux_kaio.io_submit(ctx, [op])

events = linux_kaio.io_getevents(ctx, 10)
print linux_kaio.read_event_buffer(events[0])
