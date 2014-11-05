#!/usr/bin/env python
# vim: fileencoding=utf8:et:sw=4:ts=8:sts=4

import os
from setuptools import setup, Extension


VERSION = (0, 2, 0, "")


setup(
    name="penguin",
    description="bindings to additional linux syscalls and C libraries",
    version='.'.join(filter(None, map(str, VERSION))),
    author="Travis Parker",
    author_email="travis.parker@gmail.com",
    packages=["penguin"],
    ext_modules=[
        Extension('penguin.fds',
            ['src/fds.c'],
            extra_compile_args=["-I."]),
        Extension('penguin.signals',
            ['src/signals.c'],
            extra_compile_args=["-I."]),
        Extension('penguin.posix_aio',
            ['src/posix_aio.c'],
            extra_compile_args=["-I."],
            extra_link_args=['-laio', '-lrt']),
        Extension('penguin.linux_kaio',
            ['src/linux_kaio.c'],
            extra_compile_args=["-I.", "-idirafter", "./src/missing-headers"],
            extra_link_args=['-laio']),
        Extension('penguin.sysv_ipc',
            ['src/sysv_ipc.c'],
            extra_compile_args=["-I."]),
        Extension('penguin.posix_ipc',
            ['src/posix_ipc.c'],
            extra_compile_args=["-I."],
            extra_link_args=['-lrt']),
    ],
)
