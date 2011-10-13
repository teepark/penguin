#!/usr/bin/env sh

gcc -fPIC -I/usr/local/include/python3.1 -I. src/fds.c -shared -o penguin/fds.so
gcc -fPIC -I/usr/local/include/python3.1 -I. src/signals.c -shared -o penguin/signals.so
gcc -fPIC -I/usr/local/include/python3.1 -I. src/posix_aio.c -laio -lrt -shared -o penguin/posix_aio.so
