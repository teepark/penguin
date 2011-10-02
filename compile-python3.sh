#!/usr/bin/env sh

exec gcc -fPIC -I/usr/local/include/python3.1 eventfs/_eventfs.c -lrt -laio -shared -o _eventfs.so
