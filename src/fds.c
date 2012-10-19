#include "common.h"

#include <signal.h>
#include <unistd.h>
#include <asm/unistd.h>
#ifdef __NR_eventfd
    #include <sys/eventfd.h>
#endif
#ifdef __NR_timerfd_create
    #include <sys/timerfd.h>
#endif
#ifdef __NR_signalfd
    #include <sys/signalfd.h>
#endif


/*
 * eventfd
 */

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

    Py_BEGIN_ALLOW_THREADS
    length = read(fd, (void *)&val, 8);
    Py_END_ALLOW_THREADS

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

    Py_BEGIN_ALLOW_THREADS
    length = write(fd, (const void *)&val, 8);
    Py_END_ALLOW_THREADS

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


/*
 * timerfd
 */

/* set this to penguin.structs.itimerspec at c module import time */
static PyObject *PyItimerspec = NULL;
static PyObject *PySiginfo = NULL;

#ifdef __NR_timerfd_create
static PyObject *
unwrap_timer(const struct itimerspec *spec) {
    double timeout, interval;
    PyObject *pytimeout, *pyinterval, *args, *pyspec;

    timeout = spec->it_value.tv_sec + (spec->it_value.tv_nsec / 1E9);
    if (NULL == (pytimeout = PyFloat_FromDouble(timeout)))
        return NULL;

    interval = spec->it_interval.tv_sec + (spec->it_interval.tv_nsec / 1E9);
    if (NULL == (pyinterval = PyFloat_FromDouble(interval))) {
        Py_DECREF(pytimeout);
        return NULL;
    }

    if (NULL == (args = PyTuple_New(2))) {
        Py_DECREF(pytimeout);
        Py_DECREF(pyinterval);
        return NULL;
    }

    PyTuple_SET_ITEM(args, 0, pytimeout);
    PyTuple_SET_ITEM(args, 1, pyinterval);

    if (NULL == PyItimerspec)
        return args;

    pyspec = PyObject_Call(PyItimerspec, args, NULL);
    Py_DECREF(args);
    if (NULL == pyspec)
        return NULL;

    return pyspec;
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

static char *timerfd_settime_kwargs[] = {"interval", "absolute", NULL};

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

    if (PyObject_IsTrue(absolute))
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


/*
 * signalfd
 */

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
    PyObject *args, *item, *result;

    if (NULL == (args = PyTuple_New(2)))
        return NULL;

    if (NULL == (item = PyInt_FromLong((long)(info->ssi_signo)))) {
        Py_DECREF(args);
        return NULL;
    }
    PyTuple_SET_ITEM(args, 0, item);

    if (NULL == (item = PyInt_FromLong((long)(info->ssi_code)))) {
        Py_DECREF(args);
        return NULL;
    }
    PyTuple_SET_ITEM(args, 1, item);

    if (NULL == PySiginfo)
        return args;

    result = PyObject_Call(PySiginfo, args, NULL);
    Py_DECREF(args);
    if (NULL == result)
        return NULL;

    return result;
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

    Py_BEGIN_ALLOW_THREADS
    length = read(fd, (void *)&info, sizeof(struct signalfd_siginfo));
    Py_END_ALLOW_THREADS

    if (length < 0) {
        PyErr_SetFromErrno(PyExc_IOError);
        return NULL;
    }

    return unwrap_siginfo(&info);
}
#endif /* __NR_signalfd */


/*
 * module
 */

static PyMethodDef methods[] = {
#ifdef __NR_eventfd
    {"eventfd", python_eventfd, METH_VARARGS,
        "create a file descriptor for event notification\n\
\n\
see `man 2 eventfd` for more complete documentation\n\
\n\
:param initval: where to initialize the new eventfd's counter (default 0)\n\
:type initval: non-negative int\n\
\n\
:param flags:\n\
    flags to apply for the new file descriptor (default 0)\n\
\n\
    this argument won't be available on kernels without eventfd2(2)\n\
:type flags: int\n\
\n\
:returns: integer, a new eventfd file descriptor"},

    {"read_eventfd", python_read_eventfd, METH_VARARGS,
        "read the counter out of an eventfd-created file descriptor\n\
\n\
utility method, equivalent to struct.unpack('Q', os.read(fd, 8))\n\
\n\
:param fd: the file descriptor from which to read an event\n\
:type fd: int\n\
\n\
:returns: integer, the value read from the eventfd descriptor"},

    {"write_eventfd", python_write_eventfd, METH_VARARGS,
        "add a value into an eventfd-created file descriptor\n\
\n\
utility method, equivalent to os.write(fd, struct.pack('Q', value))\n\
\n\
see `man eventfd` for exactly what this does\n\
\n\
:param fd: the file descriptor to be written to\n\
:type fd: int\n\
\n\
:param value: a value to add to the eventfd's counter\n\
:type value: non-negative int"},
#endif

#ifdef __NR_timerfd_create
    {"timerfd_create", python_timerfd_create, METH_VARARGS,
        "create a new timer and return a file descriptor that refers to it\n\
\n\
:param clockid: the type of clock to use (default CLOCK_REALTIME)\n\
:type clockid: int\n\
\n\
:param flags: flags to set on the new fd (default 0)\n\
:type flags: int\n\
\n\
:returns: integer, a new timerfd descriptor"},

    {"timerfd_settime", (PyCFunction)python_timerfd_settime, METH_VARARGS | METH_KEYWORDS,
        "arm or disarm the timer referred to by a timerfd file descriptor\n\
\n\
:param fd: the file descriptor to set a timer on\n\
:type fd: int\n\
\n\
:param timeout: time in seconds until the timer first triggers\n\
:type timeout: number\n\
\n\
:param interval:\n\
    interval in seconds at which to re-trigger the fd after the first fire\n\
    (default of 0 means only trigger once)\n\
:type interval: number\n\
\n\
:param absolute: if `True`, sets the timer as absolute time (default False)\n\
:type absolute: int\n\
\n\
:returns:\n\
    a two-tuple with the timer that was previously stored in this fd (time\n\
    remaining until the next trigger, interval after that)"},

    {"timerfd_gettime", python_timerfd_gettime, METH_VARARGS,
        "return the setting of the timer referred to by a file descriptor\n\
\n\
:param fd: file descriptor to read the timer from\n\
:type fd: int\n\
\n\
:returns:\n\
    a two-tuple with the timer stored in the fd (time remaining until the\n\
    trigger, interval after that)"},
#endif

#ifdef __NR_signalfd
    {"signalfd", python_signalfd, METH_VARARGS,
        "create a file descriptor that can be used to accept signals\n\
\n\
:param fd: signalfd descriptor to modify, or -1 to create a new one\n\
:type fd: int\n\
\n\
:param signals: signals to receive on the signalfd\n\
:type signals: iterable of ints\n\
\n\
:param flags: flags to apply to the signalfd\n\
:type flags: int\n\
\n\
:returns: integer, a new signalfd descriptor"},

    {"read_signalfd", python_read_signalfd, METH_VARARGS,
        "read signal info from a file descriptor created with signalfd\n\
\n\
:param fd: file descriptor to read a signal from\n\
:type fd: int\n\
\n\
:returns:\n\
    a two-tuple representing the signal it received, (signum, reason_code)"},
#endif

    {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef fds_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.fds", "", -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_fds(void) {
    PyObject *module, *datatypes;
    module = PyModule_Create(&fds_module);

#else

PyMODINIT_FUNC
initfds(void) {
    PyObject *module, *datatypes;
    module = Py_InitModule("penguin.fds", methods);

#endif

    datatypes = PyImport_ImportModule("penguin.structs");

    if (NULL != datatypes && PyObject_HasAttrString(datatypes, "itimerspec"))
        PyItimerspec = PyObject_GetAttrString(datatypes, "itimerspec");

    if (NULL != datatypes && PyObject_HasAttrString(datatypes, "siginfo"))
        PySiginfo = PyObject_GetAttrString(datatypes, "siginfo");

    if (NULL == datatypes)
        PyErr_Clear();

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

#ifdef SI_USER
    PyModule_AddIntConstant(module, "SI_USER", SI_USER);
#endif
#ifdef SI_KERNEL
    PyModule_AddIntConstant(module, "SI_KERNEL", SI_KERNEL);
#endif
#ifdef SI_QUEUE
    PyModule_AddIntConstant(module, "SI_QUEUE", SI_QUEUE);
#endif
#ifdef SI_TIMER
    PyModule_AddIntConstant(module, "SI_TIMER", SI_TIMER);
#endif
#ifdef SI_MESGQ
    PyModule_AddIntConstant(module, "SI_MESGQ", SI_MESGQ);
#endif
#ifdef SI_ASYNCIO
    PyModule_AddIntConstant(module, "SI_ASYNCIO", SI_ASYNCIO);
#endif
#ifdef SI_SIGIO
    PyModule_AddIntConstant(module, "SI_SIGIO", SI_SIGIO);
#endif
#ifdef SI_TKILL
    PyModule_AddIntConstant(module, "SI_TKILL", SI_TKILL);
#endif

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
