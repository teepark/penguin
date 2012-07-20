#include "src/common.h"

#include <unistd.h>
#include <aio.h>


#ifdef _POSIX_ASYNCHRONOUS_IO
typedef struct {
    PyObject_HEAD
    char own_buf;
    struct aiocb cb;
} python_aiocb_object;

static void
python_aiocb_dealloc(python_aiocb_object *self) {
    if (self->own_buf) free((void *)self->cb.aio_buf);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyTypeObject python_aiocb_type = {
    PyObject_HEAD_INIT(&PyType_Type)
#if PY_MAJOR_VERSION < 3
    0,                                         /* ob_size */
#endif
    "penguin.posix_aio.aiocb",                 /* tp_name */
    sizeof(python_aiocb_object),               /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)python_aiocb_dealloc,          /* tp_dealloc */
    0,                                         /* tp_print */
    0,                                         /* tp_getattr */
    0,                                         /* tp_setattr */
    0,                                         /* tp_compare */
    0,                                         /* tp_repr */
    0,                                         /* tp_as_number */
    0,                                         /* tp_as_sequence */
    0,                                         /* tp_as_mapping */
    0,                                         /* tp_hash */
    0,                                         /* tp_call */
    0,                                         /* tp_str */
    0,                                         /* tp_getattro */
    0,                                         /* tp_setattro */
    0,                                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                        /* tp_flags */
    0,                                         /* tp_doc */
    0,                                         /* tp_traverse */
    0,                                         /* tp_clear */
    0,                                         /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    0,                                         /* tp_iter */
    0,                                         /* tp_iternext */
    0,                                         /* tp_methods */
    0,                                         /* tp_members */
    0,                                         /* tp_getset */
    0,                                         /* tp_base */
    0,                                         /* tp_dict */
    0,                                         /* tp_descr_get */
    0,                                         /* tp_descr_set */
    0,                                         /* tp_dictoffset */
    0,                                         /* tp_init */
    PyType_GenericAlloc,                       /* tp_alloc */
    PyType_GenericNew,                         /* tp_new */
    PyObject_Del,                              /* tp_free */
};

static python_aiocb_object *
build_aiocb(int fd, int nbytes, uint64_t offset, int signo, char *buffer) {
    python_aiocb_object *pyaiocb;
    char own_buf = NULL == buffer;

    if (NULL == buffer && !(buffer = malloc(nbytes))) {
        PyErr_SetString(PyExc_MemoryError, "nbytes too big, malloc failed");
        return NULL;
    }

    if (!(pyaiocb = PyObject_New(python_aiocb_object, &python_aiocb_type)))
        return NULL;

    memset(&pyaiocb->cb, '\0', sizeof(struct aiocb));

    pyaiocb->own_buf = own_buf;
    pyaiocb->cb.aio_fildes = fd;
    pyaiocb->cb.aio_nbytes = nbytes;
    pyaiocb->cb.aio_buf = (void *)buffer;
    pyaiocb->cb.aio_offset = offset;
    pyaiocb->cb.aio_sigevent.sigev_notify = signo ? SIGEV_SIGNAL : SIGEV_NONE;
    pyaiocb->cb.aio_sigevent.sigev_signo = signo;

    return pyaiocb;
}

static PyObject *
python_read_aiocb_buffer(PyObject *module, PyObject *args) {
    python_aiocb_object *pyaiocb;
    int nbytes = -1;

    if (!PyArg_ParseTuple(args, "O!|i", &python_aiocb_type, &pyaiocb, &nbytes))
        return NULL;

    if (nbytes < 0)
        nbytes = pyaiocb->cb.aio_nbytes;

    return PyString_FromStringAndSize((const char *)pyaiocb->cb.aio_buf, nbytes);
}

static char *aio_read_kwargs[] = {"fildes", "nbytes", "offset", "signo", NULL};

static PyObject *
python_aio_read(PyObject *module, PyObject *args, PyObject *kwargs) {
    python_aiocb_object *pyaiocb;
    int fd, nbytes, signo = 0;
    uint64_t offset = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|ii", aio_read_kwargs,
                &fd, &nbytes, &offset, &signo))
        return NULL;

    if (!(pyaiocb = build_aiocb(fd, nbytes, offset, signo, NULL)))
        return NULL;

    if (aio_read(&pyaiocb->cb)) {
        PyErr_SetFromErrno(PyExc_IOError);
        Py_DECREF(pyaiocb);
        return NULL;
    }

    return (PyObject *)pyaiocb;
}

static char *aio_write_kwargs[] = {"fildes", "data", "offset", "signo", NULL};

static PyObject *
python_aio_write(PyObject *module, PyObject *args, PyObject *kwargs) {
    python_aiocb_object *pyaiocb;
    char *data;
    int fd, nbytes, signo = 0;
    uint64_t offset = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "is#|ii", aio_write_kwargs,
                &fd, &data, &nbytes, &offset, &signo))
        return NULL;

    if (!(pyaiocb = build_aiocb(fd, nbytes, offset, signo, data)))
        return NULL;

    if (aio_write(&pyaiocb->cb)) {
        PyErr_SetFromErrno(PyExc_IOError);
        Py_DECREF(pyaiocb);
        return NULL;
    }

    return (PyObject *)pyaiocb;
}

static char *aio_fsync_kwargs[] = {"op", "fildes, signo", NULL};

static PyObject *
python_aio_fsync(PyObject *module, PyObject *args, PyObject *kwargs) {
    int op, fd, signo = 0;
    python_aiocb_object *pyaiocb;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|i", aio_fsync_kwargs,
            &op, &fd, &signo))
        return NULL;

    if (!(pyaiocb = build_aiocb(fd, 0, 0, signo, NULL)))
        return NULL;

    if (aio_fsync(op, &pyaiocb->cb)) {
        PyErr_SetFromErrno(PyExc_IOError);
        Py_DECREF(pyaiocb);
        return NULL;
    }

    Py_DECREF(pyaiocb);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_aio_error(PyObject *module, PyObject *args) {
    python_aiocb_object *pyaiocb;

    if (!PyArg_ParseTuple(args, "O!", &python_aiocb_type, &pyaiocb))
        return NULL;

    return PyInt_FromLong((long)aio_error(&pyaiocb->cb));
}

static PyObject *
python_aio_return(PyObject *module, PyObject *args) {
    python_aiocb_object *pyaiocb;
    int rc;

    if (!PyArg_ParseTuple(args, "O!", &python_aiocb_type, &pyaiocb))
        return NULL;

    if ((rc = aio_error(&pyaiocb->cb))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)rc));
        return NULL;
    }

    rc = aio_return(&pyaiocb->cb);
    if (rc == EINVAL) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)EINVAL));
        return NULL;
    }

    return PyInt_FromLong((long)rc);
}

static PyObject *
python_aio_cancel(PyObject *module, PyObject *args) {
    python_aiocb_object *pyaiocb = NULL;
    int fd, rc;

    if (!PyArg_ParseTuple(args, "i|O", &fd, &pyaiocb))
        return NULL;

    if (Py_None == (PyObject *)pyaiocb)
        pyaiocb = NULL;
    else if (&python_aiocb_type != Py_TYPE(pyaiocb)) {
        PyErr_SetString(PyExc_TypeError,
                "aiocb must be an aiocb instance or None");
        return NULL;
    }

    if (0 > (rc = aio_cancel(fd, pyaiocb ? &pyaiocb->cb : NULL))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)rc);
}
#endif


static PyMethodDef methods[] = {
#ifdef _POSIX_ASYNCHRONOUS_IO
    {"read_aiocb_buffer", python_read_aiocb_buffer, METH_VARARGS,
        "get the contents of the buffer of an aiocb object\n\
\n\
:param aiocb: the aiocb object from a previous aio operation\n\
:type aiocb: aiocb\n\
\n\
:param nbytes: the number of bytes to retrieve\n\
:type nbytes: int\n\
\n\
:returns: string, contents of the aiocb struct's buffer"},
    {"aio_read", (PyCFunction)python_aio_read, METH_VARARGS | METH_KEYWORDS,
        "queue an asynchronous read from a file descriptor\n\
\n\
:param fildesc: file descriptor to read from\n\
:type fildesc: int\n\
\n\
:param nbytes: maximum number of bytes to read\n\
:type nbytes: int\n\
\n\
:param offset: offset of the file to start the read from (default 0)\n\
:type offset: int\n\
\n\
:param signo: signal to send when the read completes (default 0 for none)\n\
:type signo: int\n\
\n\
:returns: an aiocb, which can be used to get the read results"},
    {"aio_write", (PyCFunction)python_aio_write, METH_VARARGS | METH_KEYWORDS,
        "queue an asynchronous write to a file descriptor\n\
\n\
:param fildes: file descriptor to write to\n\
:type fildes: int\n\
\n\
:param data: the data to write into the file descriptor\n\
:type data: str\n\
\n\
:param offset: offset of the file to start the write from (default 0)\n\
:type offset: int\n\
\n\
:param signo: signal to send when the write completes (default 0 for none)\n\
:type signo: int\n\
\n\
:returns: an aiocb, which can be used to get the write return value"},
    {"aio_fsync", (PyCFunction)python_aio_fsync, METH_VARARGS | METH_KEYWORDS,
        "queue an asyncronous request for an fsync\n\
\n\
:param op:\n\
    the fsync operation to perform: if O_SYNC then it behaves like\n\
    fsync(2), if it is O_DSYNC then it behaves like fdatasync(2).\n\
:type op: int\n\
\n\
:param fildes: file descriptor for which to sync completed aio operations\n\
:type fildes: int\n\
\n\
:param signo: signal to send when the fsync completes (default 0 for none)\n\
:type signo: int"},
    {"aio_error", python_aio_error, METH_VARARGS,
        "get the error status of an aio operation\n\
\n\
:param aiocb: the aiocb representing the aio operation\n\
:type aiocb: aiocb\n\
\n\
:returns: int, the direct return value of aio_error(3)"},
    {"aio_return", python_aio_return, METH_VARARGS,
        "get the return status of an aio operation\n\
\n\
:param aiocb: the aiocb representing the aio operation\n\
:type aiocb: aiocb\n\
\n\
:returns: int, what the return value of the synchronous call would have been"},
    {"aio_cancel", python_aio_cancel, METH_VARARGS,
        "cancel an outstanding aio operation\n\
\n\
:param fd: file descriptor of the operation(s) to cancel\n\
:type fd: int\n\
\n\
:param aiocb:\n\
    the specific aio operation to cancel (default of None\n\
    means attempt canceling everything outstanding for the fd)\n\
:type aiocb: aiocb\n\
\n\
:returns: one of the constants AIO_CANCELED, AIO_NOTCANCELED, or AIO_ALLDONE"},
#endif

    {NULL, NULL, 0, NULL}
};


#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef posix_aio_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.posix_aio", "", -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_posix_aio(void) {
    PyObject *module;
    if (PyType_Ready(&python_aiocb_type)) return NULL;
    module = PyModule_Create(&posix_aio_module);

#else

PyMODINIT_FUNC
initposix_aio(void) {
    PyObject *module;
    if (PyType_Ready(&python_aiocb_type)) return;
    module = Py_InitModule("penguin.posix_aio", methods);

#endif

#ifdef AIO_CANCELED
    PyModule_AddIntConstant(module, "AIO_CANCELED", AIO_CANCELED);
#endif
#ifdef AIO_NOTCANCELED
    PyModule_AddIntConstant(module, "AIO_NOTCANCELED", AIO_NOTCANCELED);
#endif
#ifdef AIO_ALLDONE
    PyModule_AddIntConstant(module, "AIO_ALLDONE", AIO_ALLDONE);
#endif

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
