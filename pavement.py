import errno
import glob
import os
from setuptools import Extension

from paver.easy import *
from paver.path import path
from paver.setuputils import setup


setup(
    name="penguin",
    description="bindings to additional linux syscalls and C libraries",
    version="0.1",
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

MANIFEST = (
    "setup.py",
    "paver-minilib.zip",
)

@task
def manifest():
    path('MANIFEST.in').write_lines('include %s' % x for x in MANIFEST)

@task
@needs('generate_setup', 'minilib', 'manifest', 'setuptools.command.sdist')
def sdist():
    pass

@task
def clean():
    for p in map(path, ('penguin.egg-info', 'dist', 'build', 'MANIFEST.in')):
        if p.exists():
            if p.isdir():
                p.rmtree()
            else:
                p.remove()
    for p in path(__file__).abspath().parent.walkfiles():
        if p.endswith(".pyc") or p.endswith(".pyo"):
            try:
                p.remove()
            except OSError, exc:
                if exc.args[0] == errno.EACCES:
                    continue
                raise
    for p in glob.glob("penguin/*.so"):
        path(p).remove()

@task
def docs():
    # have to touch the automodules to build them every time since changes to
    # the module's docstrings won't affect the timestamp of the .rst file
    sh("find docs/source/penguin -name *.rst | xargs touch")
    sh("cd docs; make html")

@task
def test():
    sh("nosetests")
