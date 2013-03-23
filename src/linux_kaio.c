#include "src/common.h"

#include <errno.h>
#include <libaio.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/utsname.h>

#define IOCB_TYPE_READ  1
#define IOCB_TYPE_WRITE 2
#define IOCB_TYPE_FSYNC 3

/* alignment must be a power of 2 */
#define ALIGNED(x) (((uintptr_t)(x) + align_to) & ~(align_to - 1))
static unsigned int align_to;

#ifndef LIBAIO_H_MISSING

/*
 * python object structs
 */
typedef struct {
    char type;
    void *buf; /* the malloc'd pointer before alignment */
    struct iocb iocb;
} iocb_with_buffer;

typedef struct {
    PyObject_HEAD
    char destroyed;
    unsigned int maxevents;
    unsigned int occupied;

    io_context_t context;
    iocb_with_buffer *cbs;
} python_iocontext_object;


/*
 * iocontext python type forward declaration
 */
static PyTypeObject python_iocontext_type;

/*
 * utility methods
 */
static python_iocontext_object *
build_context(unsigned int maxevents) {
    python_iocontext_object *pyctx;
    int err;

    if (!(pyctx = PyObject_New(
                    python_iocontext_object, &python_iocontext_type)))
        return NULL;

    if (!(pyctx->cbs = malloc(maxevents * sizeof(iocb_with_buffer)))) {
        PyObject_Del(pyctx);
        PyErr_NoMemory();
        return NULL;
    }

    pyctx->destroyed = 0;
    pyctx->occupied = 0;
    pyctx->maxevents = maxevents;
    memset(&pyctx->context, '\0', sizeof(io_context_t));

    if ((err = io_setup(maxevents, &pyctx->context))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-err));
        return NULL;
    }

    return pyctx;
}

static int
destroy_context(python_iocontext_object *self, char raise) {
    int err, i;

    if (self->destroyed) {
        if (raise)
            PyErr_SetString(PyExc_ValueError, "iocontext already destroyed");
        return 1;
    }
    self->destroyed = 1;

    for (i = 0; i < self->occupied; ++i) {
        free(self->cbs[i].buf);
    }

    free(self->cbs);
    if ((err = io_destroy(self->context))) {
        if (raise)
            PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-err));
        return 1;
    }

    return 0;
}

static iocb_with_buffer *
add_iocb(python_iocontext_object *self, char type, size_t bufsize) {
    iocb_with_buffer *iocbwb;

    if (self->occupied >= self->maxevents) {
        PyErr_SetString(PyExc_ValueError, "context already full");
        return NULL;
    }

    iocbwb = self->cbs + self->occupied++;
    iocbwb->type = type;
    if (bufsize) {
        if (!(iocbwb->buf = malloc(bufsize + align_to))) {
            PyErr_NoMemory();
            return NULL;
        }
    } else
        iocbwb->buf = NULL;

    return iocbwb;
}

static int
timespec_ify(PyObject *pytimeout, struct timespec *result) {
    double timeout;
    long seconds;

    if (pytimeout == Py_None) return 1;
    else {
        if (-1 == (timeout = PyFloat_AsDouble(pytimeout)) && PyErr_Occurred())
            return -1;
        seconds = (long)timeout;
        timeout = timeout - (double)seconds;
        result->tv_sec = seconds;
        result->tv_nsec = (long)(timeout * 1E9);
    }

    return 0;
}


/*
 * module-level python functions
 */
static PyObject *
python_io_setup(PyObject *module, PyObject *args) {
    unsigned int maxevents;
    python_iocontext_object *pyctx;

    if (!PyArg_ParseTuple(args, "I", &maxevents))
        return NULL;

    if (!(pyctx = build_context(maxevents)))
        return NULL;

    return (PyObject *)pyctx;
}


/*
 * iocontext python methods
 */
static void
python_iocontext_dealloc(python_iocontext_object *self) {
    destroy_context(self, 0);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static char *iocontext_prep_read_kwargs[] = {
    "fd", "nbytes", "offset", "eventfd", NULL};

static PyObject *
python_iocontext_prep_read(PyObject *self, PyObject *args, PyObject *kwargs) {
    python_iocontext_object *pyctx = (python_iocontext_object *)self;
    void *buf, *aligned;
    int fd, evfd = 0;
    size_t nbytes;
    long long offset = 0;
    iocb_with_buffer *iocbwb;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "in|Li",
            iocontext_prep_read_kwargs, &fd, &nbytes, &offset, &evfd))
        return NULL;

    if (!(iocbwb = add_iocb(pyctx, IOCB_TYPE_READ, nbytes)))
        return NULL;

    aligned = (void *)ALIGNED(iocbwb->buf);
    io_prep_pread(&iocbwb->iocb, fd, aligned, nbytes, offset);
    if (evfd) io_set_eventfd(&iocbwb->iocb, evfd);

    return PyInt_FromLong((long)pyctx->occupied - 1);
}

static char *iocontext_prep_write_kwargs[] = {
    "fd", "data", "offset", "eventfd", NULL};

static PyObject *
python_iocontext_prep_write(PyObject *self, PyObject *args, PyObject *kwargs) {
    int count, fd, evfd = 0;
    long long offset = 0;
    void *pybuf, *buf, *aligned;
    iocb_with_buffer *iocbwb;
    python_iocontext_object *pyctx = (python_iocontext_object *)self;

    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "is#|Li",
            iocontext_prep_write_kwargs, &fd, &pybuf, &count, &offset, &evfd))
        return NULL;

    if (!(iocbwb = add_iocb(pyctx, IOCB_TYPE_WRITE, count)))
        return NULL;

    aligned = (void *)ALIGNED(iocbwb->buf);
    memcpy(aligned, pybuf, count);
    io_prep_pwrite(&iocbwb->iocb, fd, aligned, count, offset);
    if (evfd) io_set_eventfd(&iocbwb->iocb, evfd);

    return PyInt_FromLong((long)pyctx->occupied - 1);
}

static char *iocontext_prep_fsync_kwargs[] = {"fd", "eventfd", NULL};

static PyObject *
python_iocontext_prep_fsync(PyObject *self, PyObject *args, PyObject *kwargs) {
    int fd, evfd = 0;
    iocb_with_buffer *iocbwb;
    python_iocontext_object *pyctx = (python_iocontext_object *)self;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|i",
            iocontext_prep_fsync_kwargs, &fd, &evfd))
        return NULL;

    if (!(iocbwb = add_iocb(pyctx, IOCB_TYPE_FSYNC, 0)))
        return NULL;

    io_prep_fsync(&iocbwb->iocb, fd);
    if (evfd) io_set_eventfd(&iocbwb->iocb, evfd);

    return PyInt_FromLong((long)pyctx->occupied - 1);
}

static PyObject *
python_iocontext_submit(PyObject *self, PyObject *iamnull) {
    python_iocontext_object *pyctx = (python_iocontext_object *)self;
    struct iocb **iocbs;
    int i, count;

    if (!(iocbs = malloc(pyctx->occupied * sizeof(struct iocb *))))
        return PyErr_NoMemory();

    for (i = 0; i < pyctx->occupied; ++i)
        iocbs[i] = &pyctx->cbs[i].iocb;

    count = io_submit(pyctx->context, pyctx->occupied, iocbs);
    free(iocbs);
    if (count < 0) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-count));
        return NULL;
    }

    return PyInt_FromLong((long)count);
}

static PyObject *
python_iocontext_cancel(PyObject *self, PyObject *args) {
    python_iocontext_object *pyctx = (python_iocontext_object *)self;
    int num, err;
    struct io_event ev;

    if (!PyArg_ParseTuple(args, "i", &num))
        return NULL;

    if (num > pyctx->occupied) {
        PyErr_SetString(PyExc_ValueError, "num out of range");
        return NULL;
    }

    if ((err = io_cancel(pyctx->context, &pyctx->cbs[num].iocb, &ev))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-err));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *iocontext_getevents_kwargs[] = {"max", "min", "timeout", NULL};

static PyObject *
python_iocontext_getevents(PyObject *self, PyObject *args, PyObject *kwargs) {
    python_iocontext_object *pyctx = (python_iocontext_object *)self;
    char found;
    int i, j, num, max = pyctx->occupied,
        min = 1;
    PyObject *result, *item, *pytimeout = Py_None;
    struct timespec timeout;
    struct timespec *timeoutp = &timeout;
    struct io_event *events;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|iiO",
            iocontext_getevents_kwargs, &max, &min, &pytimeout))
        return NULL;

    switch (timespec_ify(pytimeout, timeoutp)) {
        case -1:
            return NULL;
        case 1:
            timeoutp = NULL;
    }

    if (!(events = malloc(max * sizeof(struct io_event)))) {
        PyErr_NoMemory();
        return NULL;
    }

    Py_BEGIN_ALLOW_THREADS
    num = io_getevents(pyctx->context, min, max, events, timeoutp);
    Py_END_ALLOW_THREADS

    if (num < 0) {
        free(events);
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-num));
        return NULL;
    }

    result = PyList_New(pyctx->occupied);
    for (i = 0; i < pyctx->occupied; ++i) {
        found = 0;
        /* TODO: there has to be a better way to do this lookup */
        for (j = 0; j < num; ++j) {
            if (&pyctx->cbs[i].iocb != events[j].obj) continue;

            switch(pyctx->cbs[i].type) {
                case IOCB_TYPE_READ:
                    if ((long)events[j].res < 0)
                        item = PyInt_FromLong(events[j].res);
                    else {
                        item = PyString_FromStringAndSize(
                                pyctx->cbs[i].iocb.u.c.buf, events[j].res);
                    }
                    break;
                case IOCB_TYPE_WRITE:
                    item = PyInt_FromLong(events[j].res);
                    break;
                case IOCB_TYPE_FSYNC:
                    if ((long)events[j].res < 0)
                        item = PyInt_FromLong(events[j].res);
                    else
                        item = Py_True;
                    break;
                default:
                    item = Py_None;
            }
            found = 1;
            break;
        }
        if (!found) item = Py_None;

        Py_INCREF(item);
        PyList_SET_ITEM(result, i, item);
    }
    free(events);

    return result;
}

static PyMethodDef iocontext_methods[] = {
    {"prep_read", (PyCFunction)python_iocontext_prep_read,
        METH_VARARGS | METH_KEYWORDS,
        "set up a read operation on a file descriptor\n\
\n\
:param int fd: the file descriptor from which to read\n\
\n\
:param int nbytes:\n\
    the number of bytes to read.\n\
\n\
    .. note::\n\
\n\
    if doing direct I/O (O_DIRECT set on the file descriptor), this\n\
    will have to be a multiple of ``penguin.linux_kaio.ALIGN_TO``\n\
\n\
:param int offset:\n\
    the position in the file from which to begin the read (default 0)\n\
\n\
    .. note::\n\
\n\
    if doing direct I/O (O_DIRECT set on the file descriptor), this\n\
    will have to be a multiple of ``penguin.linux_kaio.ALIGN_TO``\n\
\n\
:param int eventfd:\n\
    the eventfd to notify when the read is complete (default None for no\n\
    notification)\n\
\n\
:returns: the integer index of this operation in the iocontext\n\
"},
    {"prep_write", (PyCFunction)python_iocontext_prep_write,
        METH_VARARGS | METH_KEYWORDS,
        "set up a write operation on a file descriptor\n\
\n\
:param int fd: the file descriptor to which to write\n\
\n\
:param str data: the data to write to the file\n\
\n\
:param int offset:\n\
    the position in the file from which to start (over-)writing (default 0)\n\
\n\
    .. note::\n\
\n\
    if doing direct I/O (O_DIRECT set on the file descriptor), this\n\
    will have to be a multiple of ``penguin.linux_kaio.ALIGN_TO``\n\
\n\
:param int eventfd:\n\
    the eventfd to notify when the write is complete (default None for no\n\
    notification)\n\
\n\
:returns: the integer index of this operation in the iocontext\n\
"},
    {"prep_fsync", (PyCFunction)python_iocontext_prep_fsync,
        METH_VARARGS | METH_KEYWORDS,
        "set up an fsync operation on a file descriptor\n\
\n\
:param int fd: the file descriptor to fsync\n\
\n\
:param int eventfd:\n\
    the eventfd to notify when the fsync has completed (default None for no\n\
    notification)\n\
\n\
:returns: the integer index of this operation in the iocontext\n\
"},
    {"submit", python_iocontext_submit, METH_NOARGS,
        "submit all prepared operations on this iocontext\n\
\n\
:returns: the number of io operations submitted\n\
"},
    {"cancel", python_iocontext_cancel, METH_VARARGS,
        "attempt to cancel a previously submitted io operation\n\
\n\
:param int index:\n\
    the index of the operation to cancel (this was returned by\n\
    :meth:`prep_read`, :meth:`prep_write`, or :meth:`prep_fsync`)\n\
"},
    {"getevents", (PyCFunction)python_iocontext_getevents,
        METH_VARARGS | METH_KEYWORDS,
        "retrieve the results of io operations\n\
\n\
:param int max:\n\
    maximum number of results to return (defaults to the total number of\n\
    operations in the context)\n\
\n\
:param int min: minimum number of results to return (default 1)\n\
\n\
:param timeout:\n\
    maximum time to block waiting for ``min`` events\n\
    (default None for unlimited)\n\
:type timeout: int, float or None\n\
\n\
:returns:\n\
    a list with one entry for every completed io operation in the\n\
    context, each located at the index that was returned from the ``prep_*``\n\
    method call\n\
\n\
io operations might have a ``None`` result for any one of a number of\n\
reasons:\n\
\n\
- the operation's result was returned in a previous ``getevents`` invocation\n\
\n\
- the operation wasn't yet complete but ``min`` other operations were\n\
\n\
- the operation wasn't yet complete and ``timeout`` expired\n\
\n\
- the operation was added after :meth:`submit` was called\n\
\n\
- ``max`` other operations are also complete and have their results returned\n\
\n\
the result type depends on the type of the original request:\n\
\n\
read requests\n\
    a string of the data that was read\n\
\n\
write requests\n\
    a nonnegative integer of the number of bytes written\n\
\n\
fsync requests\n\
    ``True`` for success\n\
\n\
all operation types can have negative numbers as a result, in that case it\n\
is ``-errno``\n\
"},
    {NULL, NULL, 0, NULL}
};


/*
 * iocontext python type
 */
static PyTypeObject python_iocontext_type = {
    PyObject_HEAD_INIT(&PyType_Type)
#if PY_MAJOR_VERSION < 3
    0,                                         /* ob_size */
#endif
    "penguin.linux_kaio.iocontext",            /* tp_name */
    sizeof(python_iocontext_object),           /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)python_iocontext_dealloc,      /* tp_dealloc */
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
    iocontext_methods,                         /* tp_methods */
    0,                                         /* tp_members */
    0,                                         /* tp_getset */
    0,                                         /* tp_base */
    0,                                         /* tp_dict */
    0,                                         /* tp_descr_get */
    0,                                         /* tp_descr_set */
    0,                                         /* tp_dictoffset */
    0,                                         /* tp_init */
    0,                                         /* tp_alloc */
    0,                                         /* tp_new */
    0,                                         /* tp_free */
};

#endif /* ndef LIBAIO_H_MISSING */


/*
 * module methods struct
 */
static PyMethodDef module_methods[] = {

#ifndef LIBAIO_H_MISSING
    {"io_setup", python_io_setup, METH_VARARGS,
        "create an iocontext object\n\
\n\
:param int maxevents:\n\
    maximum number of events the iocontext will be capable of accepting\n\
\n\
:returns: an iocontext object\n\
"},
#endif /* ndef LIBAIO_H_MISSING */

    {NULL, NULL, 0, NULL}
};


/*
 * module initialization
 */
static void
alignment_size(void) {
    struct utsname *system;
    int rc, cmp;

    if (!(system = malloc(sizeof(struct utsname)))) {
        PyErr_NoMemory();
        return;
    }

    if (0 > (rc = uname(system))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return;
    }

    align_to = memcmp("2.6", system->release, 3) > 0 ? 4096 : 512;
    free(system);
}

#if PY_MAJOR_VERSION >= 3

static struct PyModuleDef linux_kaio_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.linux_kaio",
    "",
    -1, module_methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_linux_kaio(void) {
    if (PyType_Ready(&python_iocontext_type)) return NULL;
    PyObject *module = PyModule_Create(&linux_kaio_module);
    alignment_size();
    PyModule_AddIntConstant(module, "ALIGN_TO", align_to);
    PyModule_AddObject(module, "iocontext",
            (PyObject *)(&python_iocontext_type));
    return module;
}

#else

PyMODINIT_FUNC
initlinux_kaio(void) {
    if (PyType_Ready(&python_iocontext_type)) return;
    PyObject *module = Py_InitModule("penguin.linux_kaio", module_methods);
    alignment_size();
    PyModule_AddIntConstant(module, "ALIGN_TO", align_to);
    PyModule_AddObject(module, "iocontext",
            (PyObject *)(&python_iocontext_type));
};

#endif
