#include "src/common.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <semaphore.h>
#include <time.h>

static PyObject *py_mq_attr = NULL;
static PyObject *sysv_shm = NULL;
static PyObject *mmap_type = NULL;


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

int
find_sem_ptr(PyObject *obj, void **result) {
    if (PyInt_Check(obj) || PyLong_Check(obj)) {
        /* allow int/long, it's just the integer pointer value
           itself this is what python_sem_open is returning */
        *result = PyLong_AsVoidPtr(obj);
        if (PyErr_Occurred()) return -1;
        return 0;
    } else if (PyMemoryView_Check(obj)) {
        /* allow a memoryview which wraps a shared
           memory object from the sysv_ipc module */
        obj = PyMemoryView_GET_BASE(obj);
        if ((PyObject *)Py_TYPE(obj) == sysv_shm) {
            memcpy(result, (char *)obj + sizeof(PyObject), sizeof(void *));
            return 0;
        }
    } else if ((PyObject *)Py_TYPE(obj) == mmap_type) {
        /* allow an mmap object from the mmap module */
        memcpy(result, (char *)obj + sizeof(PyObject), sizeof(void *));
        return 0;
    }

    PyErr_SetString(PyExc_TypeError, "can't pull a pointer from this type");
    return -1;
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

static char *semopen_kwargs[] = {"name", "flags", "mode", "value", NULL};

static PyObject *
python_sem_open(PyObject *module, PyObject *args, PyObject *kwargs) {
    char *name;
    int flags = 0;
    unsigned int mode = 0, value = 0;
    sem_t *semp;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|iii", semopen_kwargs,
                &name, &flags, &mode, &value))
        return NULL;

    if (SEM_FAILED == (semp = sem_open(name, flags, (mode_t)mode, value))) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyLong_FromVoidPtr(semp);
}

static PyObject *
python_sem_close(PyObject *module, PyObject *args) {
    PyObject *pysem;
    sem_t *semp;

    if (!PyArg_ParseTuple(args, "O", &pysem))
        return NULL;

    if (find_sem_ptr(pysem, (void **)&semp) < 0)
        return NULL;

    if (sem_close(semp) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_sem_unlink(PyObject *module, PyObject *args) {
    char *name;

    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    if (sem_unlink(name) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_sem_init(PyObject *module, PyObject *args) {
    PyObject *pysem;
    void *mem;
    int pshared;
    unsigned int value;

    if (!PyArg_ParseTuple(args, "Oii", &pysem, &pshared, &value))
        return NULL;

    if (find_sem_ptr(pysem, &mem) < 0)
        return NULL;

    if (sem_init((sem_t *)mem, pshared, value) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_sem_destroy(PyObject *module, PyObject *args) {
    PyObject *pysem;
    void *mem;

    if (!PyArg_ParseTuple(args, "O", &pysem))
        return NULL;

    if (find_sem_ptr(pysem, &mem) < 0)
        return NULL;

    if (sem_destroy(mem) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_sem_getvalue(PyObject *module, PyObject *args) {
    PyObject *pysem;
    sem_t *semp;
    int val;

    if (!PyArg_ParseTuple(args, "O", &pysem))
        return NULL;

    if (find_sem_ptr(pysem, (void **)&semp) < 0)
        return NULL;

    if (sem_getvalue(semp, &val) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)val);
}

static PyObject *
python_sem_post(PyObject *module, PyObject *args) {
    PyObject *pysem;
    sem_t *semp;

    if (!PyArg_ParseTuple(args, "O", &pysem))
        return NULL;

    if (find_sem_ptr(pysem, (void **)&semp) < 0)
        return NULL;

    if (sem_post(semp) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *semwait_kwargs[] = {"sem", "try", "timeout", NULL};

static PyObject *
python_sem_wait(PyObject *module, PyObject *args, PyObject *kwargs) {
    PyObject *pysem, *pytry = Py_False;
    sem_t *semp;
    double dtimeout = -1;
    struct timespec timeout;
    int rc;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|Od", semwait_kwargs,
                &pysem, &pytry, &dtimeout))
        return NULL;

    if (find_sem_ptr(pysem, (void **)&semp))
        return NULL;

    if (PyObject_IsTrue(pytry)) {
        if (dtimeout > 0) {
            PyErr_SetString(PyExc_ValueError,
                    "both 'try' and 'timeout' can not be provided");
            return NULL;
        }
        rc = sem_trywait(semp);
    } else if (dtimeout > 0) {
        if (abs_timespec_ify(dtimeout, &timeout) < 0) {
            PyErr_SetString(PyExc_ValueError, "bad timeout float");
            return NULL;
        }
        rc = sem_timedwait(semp, &timeout);
    } else
        rc = sem_wait(semp);

    if (rc < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *shmopen_kwargs[] = {"name", "flags", "mode", NULL};

static PyObject *
python_shm_open(PyObject *module, PyObject *args, PyObject *kwargs) {
    char *name;
    int flags = 0, fd;
    unsigned int mode = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|iI", shmopen_kwargs,
                &name, &flags, &mode))
        return NULL;

    if ((fd = shm_open(name, flags, (mode_t)mode)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)fd);
}

static PyObject *
python_shm_unlink(PyObject *module, PyObject *args) {
    char *name;

    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;

    if (shm_unlink(name) < 0) {
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
    {"sem_open", (PyCFunction)python_sem_open, METH_VARARGS | METH_KEYWORDS,
        "initialize and open a named semaphore\n\
\n\
this function wraps the C function sem_open(3). see the man page for more\n\
specific details. sem_overview(7) may also be useful for understanding the\n\
difference between named and unnamed semaphores.\n\
\n\
:param str name: the '/' prefixed semaphore name\n\
\n\
:param int flags:\n\
    optional bitwise ORed flags detailing how to open the semaphore.\n\
    defaults to 0, no flags, but supports ``O_CREAT`` and ``O_EXCL``.\n\
\n\
:param int mode:\n\
    optional permissions bits of a new semaphore, for when ``O_CREAT`` was\n\
    included in ``flags``.\n\
\n\
:param int value:\n\
    optional starting value of a new semaphore, for when ``O_CREAT`` was\n\
    included in ``flags``.\n\
\n\
:returns: an integer handle to the open semaphore.\n\
"},
    {"sem_close", (PyCFunction)python_sem_close, METH_VARARGS,
        "close a named semaphore\n\
\n\
see the sem_close(3) man page for more details.\n\
\n\
:param semptr:\n\
    an object representing the in-memory location of the semaphore. this can\n\
    be an integer as returned by ``sem_open()``, a memoryview object\n\
    returned from :func:`penguin.sysv_ipc.shmat`, or an ``mmap`` object.\n\
"},
    {"sem_unlink", (PyCFunction)python_sem_unlink, METH_VARARGS,
        "remove a named semaphore\n\
\n\
this wraps the C function sem_ulink(3). see its man page for more details.\n\
\n\
:param str name: the semaphore name\n\
"},
    {"sem_post", (PyCFunction)python_sem_post, METH_VARARGS,
        "unlock a semaphore\n\
\n\
increments the value of the semaphore, un-blocking waiters if the value was\n\
previously 0.\n\
\n\
this function wraps the sem_post(3) C function, see its manpage for details.\n\
\n\
:param semptr:\n\
    an object representing the in-memory location of the semaphore. this can\n\
    be an integer as returned by ``sem_open()``, a memoryview object\n\
    returned from :func:`penguin.sysv_ipc.shmat`, or an ``mmap`` object.\n\
"},
    {"sem_wait", (PyCFunction)python_sem_wait, METH_VARARGS | METH_KEYWORDS,
        "lock/wait on a semaphore\n\
\n\
decrement the value of the semaphore, potentially waiting until the\n\
decrement wouldn't result in a negative number.\n\
\n\
this wraps one of the C functions sem_wait(3), sem_trywait(3) or\n\
sem_timedwait(3) (depending on arguments). see the man pages for details.\n\
\n\
:param semptr:\n\
    an object representing the in-memory location of the semaphore. this can\n\
    be an integer as returned by ``sem_open()``, a memoryview object\n\
    returned from :func:`penguin.sysv_ipc.shmat`, or an ``mmap`` object.\n\
\n\
:param bool try:\n\
    if ``True`` and this decrement would block, instead causes the call to\n\
    fail with ``AGAIN``. defaults to ``False``.\n\
\n\
:param float timeout:\n\
    optional maximum time to wait if the operation would block, before\n\
    failing with ``ETIMEDOUT``. defaults to no limit.\n\
"},
    {"sem_init", (PyCFunction)python_sem_init, METH_VARARGS,
        "initialize an unnamed semaphore\n\
\n\
see the man page for sem_init(3) for more details.\n\
\n\
:param semptr:\n\
    an object representing the in-memory location of the semaphore. this can\n\
    be an integer as returned by ``sem_open()``, a memoryview object\n\
    returned from :func:`penguin.sysv_ipc.shmat`, or an ``mmap`` object.\n\
\n\
:param bool pshared:\n\
    whether we are intializing a process-shared semaphore or one only for\n\
    sharing across threads of a single process.\n\
\n\
:param int value: starting value of the newly initialized semaphore.\n\
"},
    {"sem_destroy", (PyCFunction)python_sem_destroy, METH_VARARGS,
        "destroy an unnamed semaphore\n\
\n\
see the sem_destroy(3) man page for more details.\n\
\n\
:param semptr:\n\
    an object representing the in-memory location of the semaphore. this can\n\
    be an integer as returned by ``sem_open()``, a memoryview object\n\
    returned from :func:`penguin.sysv_ipc.shmat`, or an ``mmap`` object.\n\
"},
    {"sem_getvalue", (PyCFunction)python_sem_getvalue, METH_VARARGS,
        "get the value of a semaphore\n\
\n\
see the man page for sem_getvalue(3) for more details.\n\
\n\
:param semptr:\n\
    an object representing the in-memory location of the semaphore. this can\n\
    be an integer as returned by ``sem_open()``, a memoryview object\n\
    returned from :func:`penguin.sysv_ipc.shmat`, or an ``mmap`` object.\n\
\n\
:returns:\n\
    an integer of the semaphore's current value\n\
"},
    {"shm_open", (PyCFunction)python_shm_open, METH_VARARGS | METH_KEYWORDS,
        "create/open a posix shared memory object\n\
\n\
see the shm_open(3) man page for more details.\n\
\n\
:param str name: the '/' prefixed shared memory segment name\n\
\n\
:param int flags:\n\
    optional bitwise flags detailing how to open the shared memory segment.\n\
    defaults to 0, which opens the segment in read-only mode, but also\n\
    supports (at least) ``O_RDWR``, ``O_CREAT``, and ``O_EXCL``.\n\
\n\
:param int mode:\n\
    optional permissions bits of a new shared memory segment, for when\n\
    ``O_CREAT`` is included in ``flags``.\n\
\n\
:returns: an integer, the new file descriptor\n\
"},
    {"shm_unlink", (PyCFunction)python_shm_unlink, METH_VARARGS,
        "unlink/remove a shared memory object\n\
\n\
see the shm_unlink(3) man page for better detail.\n\
\n\
:param str name: the '/' prefixed shared memory segment name to remove.\n\
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

PyMODINIT_FUNC
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

    PyObject *sysvipc = PyImport_ImportModule("penguin.sysv_ipc");
    if (NULL != sysvipc && PyObject_HasAttrString(sysvipc, "_shm_type"))
        sysv_shm = PyObject_GetAttrString(sysvipc, "_shm_type");

    PyObject *mmapmod = PyImport_ImportModule("mmap");
    if (NULL != mmapmod && PyObject_HasAttrString(mmapmod, "mmap"))
        mmap_type = PyObject_GetAttrString(mmapmod, "mmap");


#if PY_MAJOR_VERSION >= 3
    return module;
#endif

}
