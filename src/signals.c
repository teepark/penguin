#include "src/common.h"

#include <signal.h>
#include <unistd.h>
#include <asm/unistd.h>

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
static int SIGNAL_COUNT = 0;

static int CHECK_SIGNALS[] = {
#ifdef SIGABRT
    SIGABRT,
#endif
#ifdef SIGALRM
    SIGALRM,
#endif
#ifdef SIGBUS
    SIGBUS,
#endif
#ifdef SIGCHLD
    SIGCHLD,
#endif
#ifdef SIGCLD
    SIGCLD,
#endif
#ifdef SIGCONT
    SIGCONT,
#endif
#ifdef SIGFPE
    SIGFPE,
#endif
#ifdef SIGHUP
    SIGHUP,
#endif
#ifdef SIGILL
    SIGILL,
#endif
#ifdef SIGINT
    SIGINT,
#endif
#ifdef SIGIO
    SIGIO,
#endif
#ifdef SIGIOT
    SIGIOT,
#endif
#ifdef SIGPIPE
    SIGPIPE,
#endif
#ifdef SIGPOLL
    SIGPOLL,
#endif
#ifdef SIGPROF
    SIGPROF,
#endif
#ifdef SIGPWR
    SIGPWR,
#endif
#ifdef SIGQUIT
    SIGQUIT,
#endif
#ifdef SIGSEGV
    SIGSEGV,
#endif
#ifdef SIGSYS
    SIGSYS,
#endif
#ifdef SIGTERM
    SIGTERM,
#endif
#ifdef SIGTRAP
    SIGTRAP,
#endif
#ifdef SIGTSTP
    SIGTSTP,
#endif
#ifdef SIGTTIN
    SIGTTIN,
#endif
#ifdef SIGTTOU
    SIGTTOU,
#endif
#ifdef SIGURG
    SIGURG,
#endif
#ifdef SIGUSR1
    SIGUSR1,
#endif
#ifdef SIGUSR2
    SIGUSR2,
#endif
#ifdef SIGVTALRM
    SIGVTALRM,
#endif
#ifdef SIGWINCH
    SIGWINCH,
#endif
#ifdef SIGXCPU
    SIGXCPU,
#endif
#ifdef SIGXFSZ
    SIGXFSZ,
#endif
    0
};

static void
init_signal_count(void) {
    int i;
    if (!SIGNAL_COUNT) {
        for (i = 0; CHECK_SIGNALS[i]; ++i);
        SIGNAL_COUNT = i;
    }
}

static PyObject *
unwrap_sigset(sigset_t *set) {
    PyObject *signals, *num;
    int i;

    if (NULL == (signals = PySet_New(NULL)))
        return NULL;

    for (i = 0; i < SIGNAL_COUNT; ++i) {
        if (sigismember(set, CHECK_SIGNALS[i])) {
            if (NULL == (num = PyInt_FromLong((long)(CHECK_SIGNALS[i])))) {
                Py_DECREF(signals);
                return NULL;
            }

            if (PySet_Add(signals, num)) {
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
#endif /* posix sigprocmask test */


static PyMethodDef methods[] = {
#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
    {"sigprocmask", python_sigprocmask, METH_VARARGS,
        "examine and change blocked signals\n\
\n\
:param how:\n\
    integer describing the operation to carry out on the mask (SIG_BLOCK to\n\
    add to the mask, SIG_UNBLOCK to remove, SIG_SETMASK to set it)\n\
:type how: int\n\
\n\
:param signals: the argument for the `how` operation\n\
:type signals: iterable of ints\n\
\n\
:returns: the list of signals in the mask before this operation"},
#endif

    {NULL, NULL, 0, NULL}
};


#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef signals_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.signals", "", -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_signals(void) {
    PyObject *module;
    module = PyModule_Create(&signals_module);

#else

PyMODINIT_FUNC
initsignals(void) {
    PyObject *module;
    module = Py_InitModule("penguin.signals", methods);

#endif

    init_signal_count();

#ifdef SIG_BLOCK
    PyModule_AddIntConstant(module, "SIG_BLOCK", SIG_BLOCK);
#endif
#ifdef SIG_UNBLOCK
    PyModule_AddIntConstant(module, "SIG_UNBLOCK", SIG_UNBLOCK);
#endif
#ifdef SIG_SETMASK
    PyModule_AddIntConstant(module, "SIG_SETMASK", SIG_SETMASK);
#endif

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
