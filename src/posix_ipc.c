#include "src/common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <time.h>

static PyObject *py_mq_attr = NULL;


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
    int flags = 0;
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
    size_t size = 8192;
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
        ""},
    {"mq_close", (PyCFunction)python_mq_close, METH_VARARGS,
        ""},
    {"mq_unlink", (PyCFunction)python_mq_unlink, METH_VARARGS,
        ""},
    {"mq_send", (PyCFunction)python_mq_send, METH_VARARGS | METH_KEYWORDS,
        ""},
    {"mq_receive", (PyCFunction)python_mq_receive, METH_VARARGS | METH_KEYWORDS,
        ""},
    {"mq_getattr", (PyCFunction)python_mq_getattr, METH_VARARGS,
        ""},
    {"mq_setattr", (PyCFunction)python_mq_setattr, METH_VARARGS,
        ""},
    {"mq_notify", (PyCFunction)python_mq_notify, METH_VARARGS | METH_KEYWORDS,
        ""},
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

    PyObject *structs = PyImport_ImportModule("penguin.structs");

    if (NULL != structs && PyObject_HasAttrString(structs, "mq_attr"))
        py_mq_attr = PyObject_GetAttrString(structs, "mq_attr");

#if PY_MAJOR_VERSION >= 3
    return module;
#endif

}
