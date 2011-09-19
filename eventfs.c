#include <Python.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <signal.h>


static PyObject *
python_eventfd(PyObject *module, PyObject *args) {
    PyObject *pyinitval;
    long initval;
    int flags, fd;

    if (!PyArg_ParseTuple(args, "O!i", &PyInt_Type, &pyinitval, &flags))
        return NULL;
    initval = PyInt_AsLong(pyinitval);

    if (initval < 0) {
        PyErr_SetString(PyExc_ValueError, "initval must be non-negative");
        return NULL;
    }

    if (!(fd = eventfd((unsigned int)initval, flags))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)fd);
}

static PyObject *
python_timerfd_create(PyObject *module, PyObject *args) {
    int clockid, flags, fd;

    if (!PyArg_ParseTuple(args, "ii", &clockid, &flags))
        return NULL;

    if (!(fd = timerfd_create(clockid, flags))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)fd);
}

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

static int CHECK_SIGNALS[] = {
    SIGABRT, SIGALRM, SIGBUS,  SIGCHLD,   SIGCLD,   SIGCONT, SIGFPE, SIGHUP,
    SIGILL,  SIGINT,  SIGIO,   SIGIOT,    SIGPIPE,  SIGPOLL, SIGPROF, SIGPWR,
    SIGQUIT, SIGSEGV, SIGSYS,  SIGTERM,   SIGTRAP,  SIGTSTP, SIGTTIN, SIGTTOU,
    SIGURG,  SIGUSR1, SIGUSR2, SIGVTALRM, SIGWINCH, SIGXCPU, SIGXFSZ };
#define SIG_COUNT 31

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
python_timerfd_settime(PyObject *module, PyObject *args) {
    int fd, flags;
    double timeout, interval = 0;
    struct itimerspec inspec, outspec;

    if (!PyArg_ParseTuple(args, "iid|d", &fd, &flags, &timeout, &interval))
        return NULL;

    wrap_timer(timeout, interval, &inspec);

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

static PyObject *
python_signalfd(PyObject *module, PyObject *args) {
    int fd, flags;
    sigset_t mask;
    PyObject *signals, *result;

    sigemptyset(&mask);

    if (!PyArg_ParseTuple(args, "iOi", &fd, &signals, &flags))
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


static PyMethodDef methods[] = {
    {"eventfd", python_eventfd, METH_VARARGS,
        "create a file descriptor for event notification"},

    {"timerfd_create", python_timerfd_create, METH_VARARGS,
        "create a new timer and return a file descriptor that refers to it"},
    {"timerfd_settime", python_timerfd_settime, METH_VARARGS,
        "arm or disarm the timer referred to by a file descriptor"},
    {"timerfd_gettime", python_timerfd_gettime, METH_VARARGS,
        "return the setting of the timer referred to by a file descriptor"},

    {"signalfd", python_signalfd, METH_VARARGS,
        "create a file descriptor that can be used to accept signals"},

    {"sigprocmask", python_sigprocmask, METH_VARARGS,
        "examine and change blocked signals"},

    {NULL, NULL, 0, NULL}
};


PyMODINIT_FUNC
initeventfs(void) {
    PyObject *module;

    module = Py_InitModule("eventfs", methods);

    PyModule_AddIntConstant(module, "EFD_NONBLOCK", EFD_NONBLOCK);
    PyModule_AddIntConstant(module, "EFD_CLOEXEC", EFD_CLOEXEC);

    PyModule_AddIntConstant(module, "CLOCK_REALTIME", CLOCK_REALTIME);
    PyModule_AddIntConstant(module, "CLOCK_MONOTONIC", CLOCK_MONOTONIC);

    PyModule_AddIntConstant(module, "TFD_NONBLOCK", TFD_NONBLOCK);
    PyModule_AddIntConstant(module, "TFD_CLOEXEC", TFD_CLOEXEC);

    PyModule_AddIntConstant(module, "TFD_TIMER_ABSTIME", TFD_TIMER_ABSTIME);

    PyModule_AddIntConstant(module, "SFD_NONBLOCK", SFD_NONBLOCK);
    PyModule_AddIntConstant(module, "SFD_CLOEXEC", SFD_CLOEXEC);

    PyModule_AddIntConstant(module, "SIG_BLOCK", SIG_BLOCK);
    PyModule_AddIntConstant(module, "SIG_UNBLOCK", SIG_UNBLOCK);
    PyModule_AddIntConstant(module, "SIG_SETMASK", SIG_SETMASK);
}
