import errno, os, time, signal
from penguin import posix_aio, signals, fds

signals.sigprocmask(signals.SIG_BLOCK, [signal.SIGIO])
sfd = fds.signalfd(-1, [signal.SIGIO])

fd = os.open("TODO", os.O_RDONLY)
max, min = 0, 500
xmax, xmin = 0, 0
dmax, dmin = 0, 500
dxmax, dxmin = 0, 0
total = 0
dtotal = 0
count = 50000
for i in xrange(count):
    t = time.time()
    op = posix_aio.aio_read(fd, 256, signo=signal.SIGIO)
    t = time.time() - t
    total += t

    fds.read_signalfd(sfd)
    posix_aio.aio_return(op)

    if t > max:
        max = t
        xmax = i
    if t < min:
        min = t
        xmin = i
    t = time.time()
    del op
    t = time.time() - t
    dtotal += t
    if t > dmax:
        dmax = t
        dxmax = i
    if t < dmin:
        dmin = t
        dxmin = i
    if i and not i % 500:
        print "working (%d)" % i

print "us avg:       %7.2f" % (total * 1000000 / count)
print "us spread:    %7.2f(%5d) %7.2f(%5d)" % (min * 1000000, xmin, max * 1000000, xmax)
print "us del avg:   %7.2f" % (dtotal * 1000 / count)
print "us del times: %7.2f(%5d) %7.2f(%5d)" % (dmin * 1000000, dxmin, dmax * 1000000, dxmax)
