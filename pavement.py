import errno
import os
from setuptools import Extension

from paver.easy import *
from paver.path import path
from paver.setuputils import setup


setup(
    name="eventfs",
    description="bindings to linux 2.6 eventfs facilities",
    version="0.1",
    author="Travis Parker",
    author_email="travis.parker@gmail.com",
    ext_modules=[Extension(
        'eventfs',
        ['eventfs.c'],
        )],
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
    for p in map(path, ('eventfs.egg-info', 'dist', 'build', 'MANIFEST.in')):
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

@task
def test():
    sh("nosetests")
