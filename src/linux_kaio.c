#include "src/common.h"

#include <errno.h>
#include <libaio.h>
#include <unistd.h>

typedef struct {
    PyObject_HEAD
    char released;
    io_context_t context;
} python_context_object;

static void
python_context_dealloc(python_context_object *self) {
    if (!self->released)
        /* nothing useful to do with an error return here */
        io_destroy(self->context);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyTypeObject python_context_type = {
    PyObject_HEAD_INIT(&PyType_Type)
#if PY_MAJOR_VERSION < 3
    0,                                         /* ob_size */
#endif
    "penguin.linux_kaio.context",              /* tp_name */
    sizeof(python_context_object),             /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)python_context_dealloc,        /* tp_dealloc */
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

static python_context_object *
build_context(unsigned maxevents) {
    python_context_object *pycontext;
    int err;

    if (!(pycontext = PyObject_New(
            python_context_object, &python_context_type)))
        return NULL;

    pycontext->released = 0;
    memset(&pycontext->context, '\0', sizeof(io_context_t));

    if ((err = io_setup(maxevents, &pycontext->context))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-err));
        return NULL;
    }

    return pycontext;
}

typedef struct {
    PyObject_HEAD
    char own_buf;
    struct iocb cb;
} python_iocb_object;

static void
python_iocb_dealloc(python_iocb_object *self) {
    if (self->own_buf) free((void *)self->cb.u.c.buf);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyTypeObject python_iocb_type = {
    PyObject_HEAD_INIT(&PyType_Type)
#if PY_MAJOR_VERSION < 3
    0,                                         /* ob_size */
#endif
    "penguin.linux_kaio.iocb",                 /* tp_name */
    sizeof(python_iocb_object),                /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)python_iocb_dealloc,           /* tp_dealloc */
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

static python_iocb_object *
build_iocb(char own_buf) {
    python_iocb_object *pyiocb;

    if (!(pyiocb = PyObject_New(python_iocb_object, &python_iocb_type)))
        return NULL;
    pyiocb->own_buf = own_buf;

    return pyiocb;
}

typedef struct {
    PyObject_HEAD
    struct io_event event;
} python_event_object;

static void
python_event_dealloc(python_event_object *self) {
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyTypeObject python_event_type = {
    PyObject_HEAD_INIT(&PyType_Type)
#if PY_MAJOR_VERSION < 3
    0,                                         /* ob_size */
#endif
    "penguin.linux_kaio.event",                /* tp_name */
    sizeof(python_event_object),               /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)python_event_dealloc,          /* tp_dealloc */
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

static python_event_object *
build_event(struct io_event event) {
    python_event_object *pyevent;

    if (!(pyevent = PyObject_New(python_event_object, &python_event_type)))
        return NULL;
    pyevent->event = event;

    return pyevent;
}


static PyObject *
python_io_setup(PyObject *module, PyObject *args) {
    unsigned maxevents;
    python_context_object *pycontext;

    if (!PyArg_ParseTuple(args, "I", &maxevents))
        return NULL;

    if (!(pycontext = build_context(maxevents)))
        return NULL;

    return (PyObject *)pycontext;
}

static PyObject *
python_io_destroy(PyObject *module, PyObject *args) {
    int err;
    python_context_object *pycontext;

    if (!PyArg_ParseTuple(args, "O!", &python_context_type, &pycontext))
        return NULL;

    if (pycontext->released) {
        PyErr_SetString(PyExc_ValueError, "io context already released");
        return NULL;
    }
    pycontext->released = 1;

    if ((err = io_destroy(pycontext->context))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-err));
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *io_prep_pread_kwargs[] = {
    "fd", "count", "offset", "eventfd", NULL};

static PyObject *
python_io_prep_pread(PyObject *module, PyObject *args, PyObject *kwargs) {
    python_iocb_object *pyiocb;
    void *buf;
    int fd, evfd = 0;
    size_t count;
    long long offset = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "in|Li",
            io_prep_pread_kwargs, &fd, &count, &offset, &evfd))
        return NULL;

    if (!(buf = malloc(count))) {
        PyErr_NoMemory();
        return NULL;
    }

    if (!(pyiocb = build_iocb(1)))
        return NULL;

    io_prep_pread(&pyiocb->cb, fd, buf, count, offset);
    if (evfd) io_set_eventfd(&pyiocb->cb, evfd);

    return (PyObject *)pyiocb;
}

static char *io_prep_pwrite_kwargs[] = {
    "fd", "data", "offset", "eventfd", NULL};

static PyObject *
python_io_prep_pwrite(PyObject *module, PyObject *args, PyObject *kwargs) {
    python_iocb_object *pyiocb;
    void *buf;
    int fd, evfd = 0;
    size_t count;
    long long offset = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "is#|Li",
            io_prep_pwrite_kwargs, &fd, &buf, &count, &offset, &evfd))
        return NULL;

    if (!(pyiocb = build_iocb(0)))
        return NULL;

    io_prep_pwrite(&pyiocb->cb, fd, buf, count, offset);
    if (evfd) io_set_eventfd(&pyiocb->cb, evfd);

    return (PyObject *)pyiocb;
}

static PyObject *
python_io_submit(PyObject *module, PyObject *args) {
    int rc;
    long nr, i = 0;
    struct iocb **iocbpp;
    PyObject *iter, *item, *pyiocbs;
    python_context_object *pycontext;

    if (!PyArg_ParseTuple(args, "O!O",
            &python_context_type, (PyObject *)&pycontext, &pyiocbs))
        return NULL;

    if (0 > (nr = (long)PyObject_Length(pyiocbs)))
        return NULL;

    iocbpp = malloc(nr * sizeof(struct iocb *));

    if (!(iter = PyObject_GetIter(pyiocbs)))
        return NULL;

    while ((item = PyIter_Next(iter))) {
        if (&python_iocb_type != Py_TYPE(item)) {
            Py_DECREF(item);
            Py_DECREF(iter);
            PyErr_SetString(PyExc_TypeError, "iocbs must be iocb instances");
            return NULL;
        }

        iocbpp[i++] = &((python_iocb_object *)item)->cb;

        Py_DECREF(item);
    }
    Py_DECREF(iter);

    if (0 > (rc = io_submit(pycontext->context, nr, iocbpp))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-rc));
        return NULL;
    }

    return PyInt_FromLong((long)rc);
}

static char *io_getevents_kwargs[] = {
    "context", "nr", "min_nr", "timeout", NULL};

static PyObject *
python_io_getevents(PyObject *module, PyObject *args, PyObject *kwargs) {
    int i, rc;
    long seconds, nr, min_nr = 1;
    double timeout;
    PyObject *result, *pytimeout = Py_None;
    struct io_event *events;
    struct timespec timeoutdata;
    struct timespec *timeoutdatap = &timeoutdata;
    python_context_object *pycontext;
    python_event_object *pyevent;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!l|lO",
            io_getevents_kwargs, &python_context_type, (PyObject **)&pycontext,
            &nr, &min_nr, &pytimeout))
        return NULL;

    if (!(events = (struct io_event *)malloc(nr * sizeof(struct io_event)))) {
        PyErr_NoMemory();
        return NULL;
    }

    if (pytimeout == Py_None)
        timeoutdatap = NULL;
    else {
        if (-1 == (timeout = PyFloat_AsDouble(pytimeout)) && PyErr_Occurred())
            return NULL;

        seconds = (long)timeout;
        timeout = timeout - (double)seconds;
        timeoutdata.tv_sec = seconds;
        timeoutdata.tv_nsec = (long)(timeout * 1E9);
    }

    if (0 > (rc = io_getevents(
            pycontext->context, min_nr, nr, events, timeoutdatap))) {
        PyErr_SetObject(PyExc_IOError, PyInt_FromLong((long)-rc));
        return NULL;
    }

    if (!(result = PyList_New(rc)))
        return NULL;

    for (i = 0; i < rc; ++i) {
        if (!(pyevent = build_event(events[i]))) {
            Py_DECREF(result);
            return NULL;
        }

        PyList_SET_ITEM(result, i, (PyObject *)pyevent);
    }
    free(events);

    return result;
}

static PyObject *
python_read_iocb_buffer(PyObject *module, PyObject *pyiocb) {
    if (&python_iocb_type != Py_TYPE(pyiocb)) {
        PyErr_SetString(PyExc_TypeError,
                "read_iocb_buffer argument must be an iocb instance");
        return NULL;
    }
    struct io_iocb_common c = ((python_iocb_object *)pyiocb)->cb.u.c;
    return PyString_FromStringAndSize(c.buf, c.nbytes);
}

static PyObject *
python_read_event_buffer(PyObject *module, PyObject *pyevent) {
    if (&python_event_type != Py_TYPE(pyevent)) {
        PyErr_SetString(PyExc_TypeError,
                "read_event_buffer argument must be an event instance");
        return NULL;
    }
    struct io_event event = ((python_event_object *)pyevent)->event;
    return PyString_FromStringAndSize(event.obj->u.c.buf, event.res);
}


static PyMethodDef methods[] = {
    {"io_setup", python_io_setup, METH_VARARGS,
        "blahblah docstring"},

    {"io_destroy", python_io_destroy, METH_VARARGS,
        "blahbalh docstring"},

    {"io_prep_pread", (PyCFunction)python_io_prep_pread,
        METH_VARARGS | METH_KEYWORDS,
        "blah blah docstring"},

    {"io_prep_pwrite", (PyCFunction)python_io_prep_pwrite,
        METH_VARARGS | METH_KEYWORDS,
        "blah blah docstring"},

    {"io_submit", python_io_submit, METH_VARARGS,
        "blh blah docstring"},

    {"io_getevents", (PyCFunction)python_io_getevents,
        METH_VARARGS | METH_KEYWORDS,
        "blah blah docstrong"},

    {"read_iocb_buffer", python_read_iocb_buffer, METH_O,
        "blah blah docstring"},

    {"read_event_buffer", python_read_event_buffer, METH_O,
        "blah blah docstring"},

    {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef linux_kaio_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.linux_kaio", "", -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_linux_kaio(void) {
    PyObject *module;
    module = PyModule_Create(&linux_kaio_module);

#else

PyMODINIT_FUNC
initlinux_kaio(void) {
    PyObject *module;
    module = Py_InitModule("penguin.linux_kaio", methods);

#endif

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
