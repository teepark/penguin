#include <Python.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>


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
    int fd, flags, signum;
    sigset_t mask;
    PyObject *signals, *iter, *item, *pysignum, *result;

    if (!PyArg_ParseTuple(args, "iOi", &fd, &signals, &flags))
        return NULL;

    if (NULL == (iter = PyObject_GetIter(signals)))
        return NULL;

    sigemptyset(&mask);

    while ((item = PyIter_Next(iter))) {
        if (NULL == (pysignum = PyNumber_Int(item))) {
            Py_DECREF(iter);
            Py_DECREF(item);
            return NULL;
        }

        if (-1 == (signum = PyInt_AsLong(pysignum)) && PyErr_Occurred()) {
            Py_DECREF(iter);
            Py_DECREF(item);
            Py_DECREF(pysignum);
            return NULL;
        }

        if (sigaddset(&mask, signum) < 0) {
            Py_DECREF(iter);
            Py_DECREF(item);
            Py_DECREF(pysignum);
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }

        Py_DECREF(pysignum);
        Py_DECREF(item);
    }
    Py_DECREF(iter);

    if ((fd = signalfd(fd, &mask, flags)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (NULL == (result = PyInt_FromLong((long)fd)))
        return NULL;

    return result;
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
}
