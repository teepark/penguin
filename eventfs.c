#include <Python.h>

#include <signal.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>


#ifdef __NR_eventfd
static PyObject *
python_eventfd(PyObject *module, PyObject *args) {
    unsigned int initval = 0;
    int fd, flags = 0;

#ifdef __NR_eventfd2
    if (!PyArg_ParseTuple(args, "|Ii", &initval, &flags))
#else
    flags = 0;
    if (!PyArg_ParseTuple(args, "|I", &initval))
#endif
        return NULL;

    if (!(fd = eventfd(initval, flags))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)fd);
}

static PyObject *
python_read_eventfd(PyObject *module, PyObject *args) {
    int fd;
    ssize_t length;
    unsigned PY_LONG_LONG val;

    if (!PyArg_ParseTuple(args, "i", &fd))
        return NULL;

    length = read(fd, (void *)&val, 8);

    if (length < 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    if (length != 8) {
        PyErr_SetObject(PyExc_OSError, PyInt_FromLong((long)EIO));
        return NULL;
    }

    return PyLong_FromUnsignedLongLong(val);
}

static PyObject *
python_write_eventfd(PyObject *module, PyObject *args) {
    int fd;
    ssize_t length;
    unsigned PY_LONG_LONG val;

    if (!PyArg_ParseTuple(args, "iK", &fd, &val))
        return NULL;

    length = write(fd, (const void *)&val, 8);

    if (length < 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    if (length != 8) {
        PyErr_SetObject(PyExc_OSError, PyInt_FromLong((long)EIO));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}
#endif /* __NR_eventfd */

#ifdef __NR_timerfd_create
static PyObject *
unwrap_timer(const struct itimerspec *spec) {
    double timeout, interval;
    PyObject *pytimeout, *pyinterval, *pair;

    timeout = spec->it_value.tv_sec + (spec->it_value.tv_nsec / 1E9);
    if (NULL == (pytimeout = PyFloat_FromDouble(timeout)))
        return NULL;

    interval = spec->it_interval.tv_sec + (spec->it_interval.tv_nsec / 1E9);
    if (NULL == (pyinterval = PyFloat_FromDouble(interval))) {
        Py_DECREF(pytimeout);
        return NULL;
    }

    if (NULL == (pair = PyTuple_New(2))) {
        Py_DECREF(pytimeout);
        Py_DECREF(pyinterval);
        return NULL;
    }

    PyTuple_SET_ITEM(pair, 0, pytimeout);
    PyTuple_SET_ITEM(pair, 1, pyinterval);
    return pair;
}

static void
wrap_timer(double timeout, double interval, struct itimerspec *spec) {
    long seconds;

    seconds = (long)timeout;
    timeout = timeout - (double)seconds;
    spec->it_value.tv_sec = seconds;
    spec->it_value.tv_nsec = (long)(timeout * 1E9);

    seconds = (long)interval;
    interval = interval - (double)seconds;
    spec->it_interval.tv_sec = seconds;
    spec->it_interval.tv_nsec = (long)(interval * 1E9);
}

static PyObject *
python_timerfd_create(PyObject *module, PyObject *args) {
    int fd, clockid = CLOCK_REALTIME, flags = 0;

    if (!PyArg_ParseTuple(args, "|ii", &clockid, &flags))
        return NULL;

    if (!(fd = timerfd_create(clockid, flags))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)fd);
}

static char *timerfd_settime_kwargs[] = {"interval", "absolute", NULL}

static PyObject *
python_timerfd_settime(PyObject *module, PyObject *args, PyObject *kwargs) {
    int fd, flags = 0;
    double timeout, interval = 0;
    struct itimerspec inspec, outspec;
    PyObject *absolute = Py_False;

    if (!PyArg_ParseTupleAndKeywords(
                args, kwargs, "id|dO", timerfd_settime_kwargs,
                &fd, &timeout, &interval, &absolute))
        return NULL;

    wrap_timer(timeout, interval, &inspec);

    if PyObject_IsTrue(absolute)
        flags |= TFD_TIMER_ABSTIME;

    if (timerfd_settime(fd, flags, &inspec, &outspec) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return unwrap_timer(&outspec);
}

static PyObject *
python_timerfd_gettime(PyObject *module, PyObject *args) {
    int fd;
    struct itimerspec spec;

    if (!PyArg_ParseTuple(args, "i", &fd))
        return NULL;

    if (timerfd_gettime(fd, &spec) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return unwrap_timer(&spec);
}
#endif /* __NR_timerfd_create */

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
#define SIG_COUNT 31
static int CHECK_SIGNALS[] = {
    SIGABRT, SIGALRM, SIGBUS,  SIGCHLD,   SIGCLD,   SIGCONT, SIGFPE,  SIGHUP,
    SIGILL,  SIGINT,  SIGIO,   SIGIOT,    SIGPIPE,  SIGPOLL, SIGPROF, SIGPWR,
    SIGQUIT, SIGSEGV, SIGSYS,  SIGTERM,   SIGTRAP,  SIGTSTP, SIGTTIN, SIGTTOU,
    SIGURG,  SIGUSR1, SIGUSR2, SIGVTALRM, SIGWINCH, SIGXCPU, SIGXFSZ };

static PyObject *
unwrap_sigset(sigset_t *set) {
    PyObject *signals, *num;
    int i;

    if (NULL == (signals = PyList_New(0)))
        return NULL;

    for (i = 0; i < SIG_COUNT; ++i) {
        if (sigismember(set, CHECK_SIGNALS[i])) {
            if (NULL == (num = PyInt_FromLong((long)(CHECK_SIGNALS[i])))) {
                Py_DECREF(signals);
                return NULL;
            }

            if (PyList_Append(signals, num)) {
                Py_DECREF(signals);
                Py_DECREF(num);
                return NULL;
            }
            Py_DECREF(num);
        }
    }

    return signals;
}

static int
wrap_sigset(sigset_t *set, PyObject *signals) {
    int signum;
    PyObject *iter, *item, *pysignum;

    if (NULL == (iter = PyObject_GetIter(signals)))
        return -1;

    while ((item = PyIter_Next(iter))) {
        if (NULL == (pysignum = PyNumber_Int(item))) {
            Py_DECREF(iter);
            Py_DECREF(item);
            return -1;
        }

        if (-1 == (signum = PyInt_AsLong(pysignum)) && PyErr_Occurred()) {
            Py_DECREF(iter);
            Py_DECREF(item);
            Py_DECREF(pysignum);
            return -1;
        }

        if (sigaddset(set, signum) < 0) {
            Py_DECREF(iter);
            Py_DECREF(item);
            Py_DECREF(pysignum);
            PyErr_SetFromErrno(PyExc_OSError);
            return -1;
        }

        Py_DECREF(item);
        Py_DECREF(pysignum);
    }
    Py_DECREF(iter);

    return 0;
}

static PyObject *
unwrap_siginfo(struct signalfd_siginfo *info) {
    PyObject *result, *item;

    if (NULL == (result = PyTuple_New(2)))
        return NULL;

    if (NULL == (item = PyInt_FromLong((long)(info->ssi_signo)))) {
        Py_DECREF(result);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 0, item);

    if (NULL == (item = PyInt_FromLong((long)(info->ssi_code)))) {
        Py_DECREF(result);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 1, item);

    return result;
}

static PyObject *
python_sigprocmask(PyObject *module, PyObject *args) {
    int how;
    sigset_t newmask, oldmask;
    PyObject *signals;

    sigemptyset(&newmask);
    sigemptyset(&oldmask);

    if (!PyArg_ParseTuple(args, "iO", &how, &signals))
        return NULL;

    if (wrap_sigset(&newmask, signals))
        return NULL;

    if (sigprocmask(how, &newmask, &oldmask))
        return NULL;

    return unwrap_sigset(&oldmask);
}

#ifdef __NR_signalfd
static PyObject *
python_signalfd(PyObject *module, PyObject *args) {
    int fd, flags = 0;
    sigset_t mask;
    PyObject *signals, *result;

    sigemptyset(&mask);

#ifdef __NR_signalfd4
    if (!PyArg_ParseTuple(args, "iO|i", &fd, &signals, &flags))
#else
    if (!PyArg_ParseTuple(args, "iO", &fd, &signals))
#endif
        return NULL;

    if (wrap_sigset(&mask, signals))
        return NULL;

    if ((fd = signalfd(fd, &mask, flags)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (NULL == (result = PyInt_FromLong((long)fd)))
        return NULL;

    return result;
}

static PyObject *
python_read_signalfd(PyObject *module, PyObject *args) {
    int fd;
    struct signalfd_siginfo info;
    ssize_t length;

    if (!PyArg_ParseTuple(args, "i", &fd))
        return NULL;

    length = read(fd, (void *)&info, sizeof(struct signalfd_siginfo));

    if (length < 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    return unwrap_siginfo(&info);
}
#endif /* __NR_signalfd */
#endif /* posix sigprocmask test */


static PyMethodDef methods[] = {
#ifdef __NR_eventfd
    {"eventfd", python_eventfd, METH_VARARGS,
        "create a file descriptor for event notification"},
    {"read_eventfd", python_read_eventfd, METH_VARARGS,
        "read the counter out of an eventfd-created file descriptor"},
    {"write_eventfd", python_write_eventfd, METH_VARARGS,
        "add a value into an eventfd-created file descriptor"},
#endif

#ifdef __NR_timerfd_create
    {"timerfd_create", python_timerfd_create, METH_VARARGS,
        "create a new timer and return a file descriptor that refers to it"},
    {"timerfd_settime", python_timerfd_settime, METH_VARARGS | METH_KEYWORDS,
        "arm or disarm the timer referred to by a file descriptor"},
    {"timerfd_gettime", python_timerfd_gettime, METH_VARARGS,
        "return the setting of the timer referred to by a file descriptor"},
#endif

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
    {"sigprocmask", python_sigprocmask, METH_VARARGS,
        "examine and change blocked signals"},
#ifdef __NR_signalfd
    {"signalfd", python_signalfd, METH_VARARGS,
        "create a file descriptor that can be used to accept signals"},
    {"read_signalfd", python_read_signalfd, METH_VARARGS,
        "read signal info from a file descriptor created with signalfd"},
#endif
#endif

    {NULL, NULL, 0, NULL}
};


PyMODINIT_FUNC
initeventfs(void) {
    PyObject *module;

    module = Py_InitModule("eventfs", methods);

#ifdef EFD_NONBLOCK
    PyModule_AddIntConstant(module, "EFD_NONBLOCK", EFD_NONBLOCK);
#endif
#ifdef EFD_CLOEXEC
    PyModule_AddIntConstant(module, "EFD_CLOEXEC", EFD_CLOEXEC);
#endif
#ifdef EFD_SEMAPHORE
    PyModule_AddIntConstant(module, "EFD_SEMAPHORE", EFD_SEMAPHORE);
#endif
#ifdef CLOCK_REALTIME
    PyModule_AddIntConstant(module, "CLOCK_REALTIME", CLOCK_REALTIME);
#endif
#ifdef CLOCK_MONOTONIC
    PyModule_AddIntConstant(module, "CLOCK_MONOTONIC", CLOCK_MONOTONIC);
#endif
#ifdef TFD_NONBLOCK
    PyModule_AddIntConstant(module, "TFD_NONBLOCK", TFD_NONBLOCK);
#endif
#ifdef TFD_CLOEXEC
    PyModule_AddIntConstant(module, "TFD_CLOEXEC", TFD_CLOEXEC);
#endif
#ifdef SFD_NONBLOCK
    PyModule_AddIntConstant(module, "SFD_NONBLOCK", SFD_NONBLOCK);
#endif
#ifdef SFD_CLOEXEC
    PyModule_AddIntConstant(module, "SFD_CLOEXEC", SFD_CLOEXEC);
#endif
#ifdef SIG_BLOCK
    PyModule_AddIntConstant(module, "SIG_BLOCK", SIG_BLOCK);
#endif
#ifdef SIG_UNBLOCK
    PyModule_AddIntConstant(module, "SIG_UNBLOCK", SIG_UNBLOCK);
#endif
#ifdef SIG_SETMASK
    PyModule_AddIntConstant(module, "SIG_SETMASK", SIG_SETMASK);
#endif
}
