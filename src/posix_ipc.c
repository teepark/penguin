#include "src/common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <time.h>

static PyObject *py_mq_attr = NULL;


static long msgsize_max;
#define MSGSIZE_MAX msgsize_max > 0 ? msgsize_max : 8192

static void
get_msgsizemax(void) {
    FILE *fp;
    char buf[4096], *ptr = &buf[0];
    size_t len;

    if (NULL == (fp = fopen("/proc/sys/fs/mqueue/msgsize_max", "r"))) {
        msgsize_max = -1;
        return;
    }

    len = fread(ptr, 1, 4095, fp);
    ptr[len] = 0;
    msgsize_max = strtol(ptr, NULL, 10);
}

static int
pytolong(PyObject *obj, long *target) {
    if (PyInt_Check(obj)) {
        *target = PyInt_AsLong(obj);
    } else if (PyLong_Check(obj)) {
        *target = PyLong_AsLong(obj);
        if (*target == -1 && PyErr_Occurred())
            return -1;
    } else {
        PyErr_SetString(PyExc_TypeError, "int or long required");
        return -1;
    }
    return 0;
}

static int
abs_timespec_ify(double secs, struct timespec *result) {
    if (clock_gettime(CLOCK_REALTIME, result) < 0)
        return -1;
    result->tv_sec += (time_t)secs;
    result->tv_nsec += (long)((secs - (long)secs) * 1E9);
    return 0;
}

static int
parse_mqattr(PyObject *objs, struct mq_attr *attrp) {
    if (!PyTuple_Check(objs) || PyObject_Length(objs) != 4) {
        PyErr_SetString(PyExc_TypeError, "attr must be a 4-tuple");
        return -1;
    }

    if (pytolong(PyTuple_GET_ITEM(objs, 0), &attrp->mq_flags) < 0
            || pytolong(PyTuple_GET_ITEM(objs, 1), &attrp->mq_maxmsg) < 0
            || pytolong(PyTuple_GET_ITEM(objs, 2), &attrp->mq_msgsize) < 0
            || pytolong(PyTuple_GET_ITEM(objs, 3), &attrp->mq_curmsgs) < 0)
        return -1;

    return 0;
}

static PyObject *
dump_mqattr(struct mq_attr *attrp) {
    PyObject *args, *obj, *result = NULL;

    if (NULL == (args = PyTuple_New(4)))
        return NULL;

    if (NULL == (obj = PyInt_FromLong(attrp->mq_flags)))
        goto done;
    PyTuple_SET_ITEM(args, 0, obj);

    if (NULL == (obj = PyInt_FromLong(attrp->mq_maxmsg)))
        goto done;
    PyTuple_SET_ITEM(args, 1, obj);

    if (NULL == (obj = PyInt_FromLong(attrp->mq_msgsize)))
        goto done;
    PyTuple_SET_ITEM(args, 2, obj);

    if (NULL == (obj = PyInt_FromLong(attrp->mq_curmsgs)))
        goto done;
    PyTuple_SET_ITEM(args, 3, obj);

    result = PyObject_Call(py_mq_attr, args, NULL);

done:
    Py_DECREF(args);
    return result;
}

static char *mqopen_kwargs[] = {"name", "flags", "mode", "attr", NULL};

static PyObject *
python_mq_open(PyObject *module, PyObject *args, PyObject *kwargs) {
    char *name;
    int flags = O_RDONLY;
    unsigned int mode = 0;
    PyObject *pyattr = Py_None;
    int result;
    struct mq_attr attr;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|iIO", mqopen_kwargs,
                &name, &flags, &mode, &pyattr))
        return NULL;

    if (flags & O_CREAT && parse_mqattr(pyattr, &attr) < 0)
        return NULL;

    if ((result = (int)mq_open(name, flags, (mode_t)mode, &attr)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)result);
}

static PyObject *
python_mq_close(PyObject *module, PyObject *args) {
    int mqdes;

    if (!PyArg_ParseTuple(args, "i", &mqdes))
        return NULL;

    if (mq_close(mqdes) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_mq_unlink(PyObject *module, PyObject *args) {
    char *name;

    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    if (mq_unlink(name) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *mqsend_kwargs[] = {"mqdes", "msg", "msg_prio", "timeout", NULL};

static PyObject *
python_mq_send(PyObject *module, PyObject *args, PyObject *kwargs) {
    int mqdes, result;
    char *msg_ptr;
    unsigned int msg_len, msg_prio = 0;
    double dtimeout = -1;
    struct timespec timeout;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "is#|Id", mqsend_kwargs,
                &mqdes, &msg_ptr, &msg_len, &msg_prio, &dtimeout))
        return NULL;

    if (dtimeout < 0) {
        result = mq_send((mqd_t)mqdes, msg_ptr, (size_t)msg_len, msg_prio);
    } else {
        if (abs_timespec_ify(dtimeout, &timeout) < 0) {
            PyErr_SetString(PyExc_ValueError, "bad timeout float");
            return NULL;
        }
        result = mq_timedsend(
                (mqd_t)mqdes, msg_ptr, (size_t)msg_len, msg_prio, &timeout);
    }

    if (result < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *mqreceive_kwargs[] = {"mqdes", "timeout", "sizehint", NULL};

static PyObject *
python_mq_receive(PyObject *module, PyObject *args, PyObject *kwargs) {
    int mqdes;
    double dtimeout = -1;
    struct timespec timeout;
    size_t size = MSGSIZE_MAX;
    ssize_t received;
    unsigned int prio;
    PyObject *obj, *result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|dn", mqreceive_kwargs,
                &mqdes, &dtimeout, &size))
        return NULL;

    char buf[size];
    char *msg = &buf[0];

    if (dtimeout < 0) {
        received = mq_receive((mqd_t)mqdes, msg, size, &prio);
    } else {
        if (abs_timespec_ify(dtimeout, &timeout) < 0) {
            PyErr_SetString(PyExc_ValueError, "bad timeout float");
            return NULL;
        }
        received = mq_timedreceive((mqd_t)mqdes, msg, size, &prio, &timeout);
    }

    if (received < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (NULL == (obj = PyInt_FromLong((long)prio)))
        return NULL;

    if (NULL == (result = PyTuple_New(2))) {
        Py_DECREF(obj);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 0, obj);

    if (NULL == (obj = PyString_FromStringAndSize(msg, received))) {
        Py_DECREF(result);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 1, obj);

    return result;
}

static PyObject *
python_mq_getattr(PyObject *module, PyObject *args) {
    int mqdes;
    struct mq_attr attr;
    
    if (!PyArg_ParseTuple(args, "i", &mqdes))
        return NULL;

    if (mq_getattr((mqd_t)mqdes, &attr) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return dump_mqattr(&attr);
}

static PyObject *
python_mq_setattr(PyObject *module, PyObject *args) {
    int mqdes;
    PyObject *pyattr;
    struct mq_attr newattr, oldattr;

    if (!PyArg_ParseTuple(args, "iO", &mqdes, &pyattr))
        return NULL;

    if (parse_mqattr(pyattr, &newattr) < 0)
        return NULL;

    if (mq_setattr(mqdes, &newattr, &oldattr) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return dump_mqattr(&oldattr);
}

static char *mqnotify_kwargs[] = {"mqdes", "signo", NULL};

static PyObject *
python_mq_notify(PyObject *module, PyObject *args, PyObject *kwargs) {
    int mqdes, signo = 0;
    struct sigevent sigev;
    struct sigevent *sigevp = &sigev;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|i", mqnotify_kwargs,
                &mqdes, &signo))
        return NULL;

    if (signo) {
        sigev.sigev_notify = SIGEV_SIGNAL;
        sigev.sigev_signo = signo;
    } else {
        sigevp = NULL;
    }

    if (mq_notify(mqdes, sigevp) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyMethodDef module_methods[] = {
    {"mq_open", (PyCFunction)python_mq_open, METH_VARARGS | METH_KEYWORDS,
        "open and/or create a message queue\n\
\n\
this is a thin wrapper over mq_open(3). see the man page for specific details\n\
\n\
:param str name: name of the queue, must start with \"/\"\n\
\n\
:param int flags:\n\
    optional bitwise ORed flags detailing how to open the message queue.\n\
    defaults to just ``O_RDONLY``.\n\
\n\
:param int mode:\n\
    optional permissions bits for newly-created queues (therefore only\n\
    relevant if ``O_CREAT`` is included in ``flags``).\n\
\n\
:param tuple attr:\n\
    optional queue attribute four-tuple to set attributes if newly creating\n\
    a queue. see :func:`mq_getattr` for a description, though ``mq_curmsgs``\n\
    obviously has no effect here.\n\
\n\
:returns:\n\
    an integer message queue descriptor for use in other mq_* functions.\n\
"},
    {"mq_close", (PyCFunction)python_mq_close, METH_VARARGS,
        "close a message queue descriptor\n\
\n\
see the man page for mq_close(3) for more details.\n\
\n\
:param int mqdes: message queue descriptor as returned by :func:`mq_open`\n\
"},
    {"mq_unlink", (PyCFunction)python_mq_unlink, METH_VARARGS,
        "remove a message queue from the system\n\
\n\
see the mq_unlink(3) man page for more details.\n\
\n\
:param str name: the name of the message queue, must start with \"/\"\n\
"},
    {"mq_send", (PyCFunction)python_mq_send, METH_VARARGS | METH_KEYWORDS,
        "send a message to a queue\n\
\n\
this wraps the C functions mq_send(3) or mq_timedsend(3). see the man pages\n\
for more details.\n\
\n\
:param int mqdes:\n\
    the message queue descriptor, as returned by :func:`mq_open`\n\
\n\
:param str msg: the string message to send\n\
\n\
:param int msg_prio:\n\
    optional nonnegative priority with which to send the message (greater\n\
    number means higher priority). defaults to 0.\n\
\n\
:param float timeout:\n\
    optional maximum time to block, in the event that the message queue is\n\
    already full and wasn't :func:`mq_open`\'d with ``O_NONBLOCK``.\n\
"},
    {"mq_receive", (PyCFunction)python_mq_receive, METH_VARARGS | METH_KEYWORDS,
        "pull a message off of a queue\n\
\n\
see the mq_receive(3) and mq_timedreceive(3) man pages for more details.\n\
\n\
:param int mqdes: the queue descriptor, from :func:`mq_open`.\n\
\n\
:param float timeout: optional maximum time to block waiting.\n\
\n\
:param int sizehint:\n\
    optional maximum message size to support. defaults to the highest\n\
    ``msgsize_max`` that the system will allow, but the max message size for\n\
    this individual queue might have been set differently, in which case\n\
    less memory could be used.\n\
\n\
:returns:\n\
    a two-tuple of the priority with which the message was sent and the\n\
    string message itself.\n\
"},
    {"mq_getattr", (PyCFunction)python_mq_getattr, METH_VARARGS,
        "get the properties of a message queue and its descriptor\n\
\n\
see the man page for mq_getattr(3) for more details.\n\
\n\
:param int mqdes: descriptor to query, as returned by :func:`mq_open`.\n\
\n\
:returns:\n\
    a four-tuple containing:\n\
\n\
    ``mq_flags``:\n\
        0 or ``O_NONBLOCK`` for the descriptor\n\
    ``mq_maxmsg``:\n\
        max # of messages this queue supports\n\
    ``mq_msgsize``:\n\
        maximum size (in bytes) of the messages on the queue\n\
    ``mq_curmsgs``:\n\
        current number of messages on the queue\n\
\n\
    a :class:`mq_attr<penguin.structs.mq_attr>` instance is actually\n\
    returned, which is a namedtuple wrapper.\n\
"},
    {"mq_setattr", (PyCFunction)python_mq_setattr, METH_VARARGS,
        "set attrbutes of a queue\n\
\n\
see the mq_setattr(3) man page for more detail.\n\
\n\
:param int mqdes: the queue descriptor (as returned by :func:`mq_open`).\n\
\n\
:param tuple attr:\n\
    an attribute four-tuple (as returned by :func:`mq_getattr`), however the\n\
    only field that can be changed is ``mq_flags``. the rest are ignored.\n\
\n\
:returns:\n\
    an attribute four-tuple (actually\n\
    :class:`mq_attr<penguin.structs.mq_attr>`) representing the attributes\n\
    before this change.\n\
"},
    {"mq_notify", (PyCFunction)python_mq_notify, METH_VARARGS | METH_KEYWORDS,
        "[de]register for notification when a message is available\n\
\n\
this is a wrapper for the C function mq_notify(3), more detailed information\n\
is available in the man page, however this wrapper simplifies the method\n\
signature a bit.\n\
\n\
:param int mqdes: queue descriptor (as returned by :func:`mq_open`).\n\
\n\
:param int signo:\n\
    0, or a signal number. If the former case this process will be\n\
    unregistered as notification target if it is already registered, and in\n\
    the latter case it will register using ``SIGEV_SIGNAL`` and the given\n\
    signal number.\n\
"},
    {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef posix_ipc_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.posix_ipc",
    "",
    -1, module_methods,
    NULL, NULL, NULL, NULL
};

PYMODINIT_FUNC
PyInit_posix_ipc(void) {
    PyObject *module = PyModule_Create(&posix_ipc_module);

#else

PyMODINIT_FUNC
initposix_ipc(void) {
    PyObject *module = Py_InitModule("penguin.posix_ipc", module_methods);

#endif

    get_msgsizemax();

    PyObject *structs = PyImport_ImportModule("penguin.structs");

    if (NULL != structs && PyObject_HasAttrString(structs, "mq_attr"))
        py_mq_attr = PyObject_GetAttrString(structs, "mq_attr");

#if PY_MAJOR_VERSION >= 3
    return module;
#endif

}
