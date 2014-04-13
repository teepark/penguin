#include "src/common.h"

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


/* set these to penguin.structs.* at import time */
static PyObject *PyIpcPerm = NULL;
static PyObject *PyMsqidDs = NULL;
static PyObject *PySemidDs = NULL;

/* memory page size. this will be the default max size for msgrcv */
static int pagesize;


static PyObject *
pythonify_ipcperm(struct ipc_perm *perm) {
    PyObject *obj, *args, *pyperm;

    if (NULL == (args = PyTuple_New(7))) return NULL;

    if (NULL == (obj = PyInt_FromLong((long)perm->__key)))
        goto fail;
    PyTuple_SET_ITEM(args, 0, obj);

    if (NULL == (obj = PyInt_FromLong((long)perm->uid)))
        goto fail;
    PyTuple_SET_ITEM(args, 1, obj);

    if (NULL == (obj = PyInt_FromLong((long)perm->gid)))
        goto fail;
    PyTuple_SET_ITEM(args, 2, obj);

    if (NULL == (obj = PyInt_FromLong((long)perm->cuid)))
        goto fail;
    PyTuple_SET_ITEM(args, 3, obj);

    if (NULL == (obj = PyInt_FromLong((long)perm->cgid)))
        goto fail;
    PyTuple_SET_ITEM(args, 4, obj);

    if (NULL == (obj = PyInt_FromLong((long)perm->mode)))
        goto fail;
    PyTuple_SET_ITEM(args, 5, obj);

    if (NULL == (obj = PyInt_FromLong((long)perm->__seq)))
        goto fail;
    PyTuple_SET_ITEM(args, 6, obj);

    pyperm = PyObject_Call(PyIpcPerm, args, NULL);
    Py_DECREF(args);
    return pyperm;

fail:
    Py_DECREF(args);
    return NULL;
}

static PyObject *
pythonify_mqds(struct msqid_ds *mqds) {
    PyObject *obj, *args, *result;

    if (NULL == (obj = pythonify_ipcperm(&mqds->msg_perm)))
        return NULL;

    if (NULL == (args = PyTuple_New(9))) {
        Py_DECREF(obj);
        return NULL;
    }
    PyTuple_SET_ITEM(args, 0, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_stime)))
        goto fail;
    PyTuple_SET_ITEM(args, 1, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_rtime)))
        goto fail;
    PyTuple_SET_ITEM(args, 2, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_ctime)))
        goto fail;
    PyTuple_SET_ITEM(args, 3, obj);

    if (NULL == (obj = PyLong_FromUnsignedLong(mqds->__msg_cbytes)))
        goto fail;
    PyTuple_SET_ITEM(args, 4, obj);

    if (NULL == (obj = PyLong_FromUnsignedLong((unsigned long)mqds->msg_qnum)))
        goto fail;
    PyTuple_SET_ITEM(args, 5, obj);

    if (NULL == (obj = PyLong_FromUnsignedLong((unsigned long)mqds->msg_qbytes)))
        goto fail;
    PyTuple_SET_ITEM(args, 6, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_lspid)))
        goto fail;
    PyTuple_SET_ITEM(args, 7, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_lrpid)))
        goto fail;
    PyTuple_SET_ITEM(args, 8, obj);

    result = PyObject_Call(PyMsqidDs, args, NULL);
    Py_DECREF(args);
    return result;

fail:
    Py_DECREF(args);
    return NULL;
}

static PyObject *
pythonify_sds(struct semid_ds *sds) {
    PyObject *obj, *args, *result = NULL;

    if (NULL == (obj = pythonify_ipcperm(&sds->sem_perm)))
        return NULL;

    if (NULL == (args = PyTuple_New(4))) {
        Py_DECREF(obj);
        return NULL;
    }
    PyTuple_SET_ITEM(args, 0, obj);

    if (NULL == (obj = PyInt_FromLong((long)sds->sem_otime)))
        goto end;
    PyTuple_SET_ITEM(args, 1, obj);

    if (NULL == (obj = PyInt_FromLong((long)sds->sem_ctime)))
        goto end;
    PyTuple_SET_ITEM(args, 2, obj);

    if (NULL == (obj = PyLong_FromUnsignedLong(sds->sem_nsems)))
        goto end;
    PyTuple_SET_ITEM(args, 3, obj);

    result = PyObject_Call(PySemidDs, args, NULL);

end:
    Py_DECREF(args);
    return result;
}

static PyObject *
pythonify_semarray(unsigned short *semvals, unsigned long count) {
    PyObject *result, *num;
    unsigned long i;

    if (NULL == (result = PyList_New(count)))
        return NULL;

    for (i = 0; i < count; ++i) {
        if (NULL == (num = PyInt_FromLong((long)semvals[i]))) {
            Py_DECREF(result);
            return NULL;
        }
        PyList_SET_ITEM(result, i, num);
    }
    return result;
}

int
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

int
unpythonify_msginfo(PyObject *info, struct msqid_ds *mqds) {
    PyObject *obj;
    long l;

    if (!PyTuple_Check(info) || PyTuple_GET_SIZE(info) != 4) {
        PyErr_SetString(PyExc_TypeError, "msg_info must be a four-tuple");
        return -1;
    }

    obj = PyTuple_GET_ITEM(info, 0);
    if (pytolong(obj, &l)) return -1;
    mqds->msg_qbytes = (msgqnum_t)l;

    obj = PyTuple_GET_ITEM(info, 1);
    if (pytolong(obj, &l)) return -1;
    mqds->msg_perm.uid = (uid_t)l;

    obj = PyTuple_GET_ITEM(info, 2);
    if (pytolong(obj, &l)) return -1;
    mqds->msg_perm.gid = (gid_t)l;

    obj = PyTuple_GET_ITEM(info, 3);
    if (pytolong(obj, &l)) return -1;
    mqds->msg_perm.mode = (unsigned short)l;

    return 0;
}

int
unpythonify_seminfo(PyObject *info, struct semid_ds *sds) {
    PyObject *obj;
    long l;

    if (!PyTuple_Check(info) || PyTuple_GET_SIZE(info) != 3) {
        PyErr_SetString(PyExc_TypeError, "sem_info must be a three-tuple");
        return -1;
    }

    obj = PyTuple_GET_ITEM(info, 0);
    if (pytolong(obj, &l)) return -1;
    sds->sem_perm.uid = (uid_t)l;

    obj = PyTuple_GET_ITEM(info, 1);
    if (pytolong(obj, &l)) return -1;
    sds->sem_perm.gid = (gid_t)l;

    obj = PyTuple_GET_ITEM(info, 2);
    if (pytolong(obj, &l)) return -1;
    sds->sem_perm.mode = (unsigned short)l & 0x1ff;

    return 0;
}

typedef struct {
    unsigned int count;
    struct sembuf *ops;
} semops;

static int
unpythonify_semops(PyObject *pyops, semops *ops) {
    int i;
    PyObject *iter, *tuple, *obj;
    struct sembuf *op;

    if (NULL == (iter = PyObject_GetIter(pyops)))
        return -1;

    i = 0;
    while (tuple = PyIter_Next(iter)) {
        if (i == ops->count) {
            PyErr_SetString(PyExc_RuntimeError, "overrun");
            return -1;
        }
        op = &ops->ops[i++];

        if (!PyTuple_Check(tuple) || PyTuple_GET_SIZE(tuple) != 3) {
            PyErr_SetString(PyExc_ValueError, "invalid sem op tuple");
            goto fail;
        }

        obj = PyTuple_GET_ITEM(tuple, 0);
        if (PyInt_Check(obj)) {
            op->sem_num = PyInt_AS_LONG(obj) & 0xffff;
        } else if (PyLong_Check(obj)) {
            op->sem_num = PyLong_AsLong(obj) & 0xffff;
        } else {
            PyErr_SetString(PyExc_ValueError, "invalid sem op tuple");
            goto fail;
        }

        obj = PyTuple_GET_ITEM(tuple, 1);
        if (PyInt_Check(obj)) {
            op->sem_op = PyInt_AS_LONG(obj) & 0xffff;
        } else if (PyLong_Check(obj)) {
            op->sem_op = PyLong_AsLong(obj) & 0xffff;
        } else {
            PyErr_SetString(PyExc_ValueError, "invalid sem op tuple");
            goto fail;
        }

        obj = PyTuple_GET_ITEM(tuple, 2);
        if (PyInt_Check(obj)) {
            op->sem_flg = PyInt_AS_LONG(obj) & 0xffff;
        } else if (PyLong_Check(obj)) {
            op->sem_flg = PyLong_AsLong(obj) & 0xffff;
        } else {
            PyErr_SetString(PyExc_ValueError, "invalid sem op tuple");
            goto fail;
        }

        Py_DECREF(tuple);
    }
    Py_DECREF(iter);

    return 0;

fail:
    Py_DECREF(tuple);
    Py_DECREF(iter);
    return -1;
}

int
unpythonify_semarray(PyObject *pyarray, unsigned short *array) {
    PyObject *iter, *obj;
    long l;
    unsigned long i = 0;

    if (NULL == (iter = PyObject_GetIter(pyarray)))
        return -1;

    while (obj = PyIter_Next(iter)) {
        if (pytolong(obj, &l) < 0) {
            Py_DECREF(obj);
            Py_DECREF(iter);
            return -1;
        }
        array[i++] = (unsigned short)l;
        Py_DECREF(obj);
    }
    Py_DECREF(iter);

    return 0;
}

typedef struct {
    long type;
    char text[1];
} msgbuf;

static char *msgctl_kwargs[] = {"id", "cmd", "setinfo", NULL};

static PyObject *
python_msgctl(PyObject *self, PyObject *args, PyObject *kwargs) {
    int mqid, cmd;
    PyObject *setinfo = NULL;
    struct msqid_ds mqds;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|O", msgctl_kwargs,
                &mqid, &cmd, &setinfo))
        return NULL;

    if (cmd == IPC_SET && unpythonify_msginfo(setinfo, &mqds))
        return NULL;

    if (msgctl(mqid, cmd, &mqds) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (cmd == IPC_STAT)
        return pythonify_mqds(&mqds);

    Py_INCREF(Py_None);
    return Py_None;
}

int
timespec_ify(double secs, struct timespec *result) {
    result->tv_sec = (time_t)secs;
    result->tv_nsec = (long)((secs - result->tv_sec) * 1E9);
    return 0;
}


/*
 * module-level python functions
 */
static PyObject *
python_ftok(PyObject *module, PyObject *args) {
    key_t key;
    char *filepath;
    int projid;

    if (!PyArg_ParseTuple(args, "si", &filepath, &projid))
        return NULL;

    if ((key = ftok(filepath, projid)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)key);
}

static char *msgget_kwargs[] = {"key", "flags", NULL};

static PyObject *
python_msgget(PyObject *module, PyObject *args, PyObject *kwargs) {
    key_t key = 0;
    int qid, msgflag = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "l|i", msgget_kwargs,
                (long *)&key, &msgflag))
        return NULL;

    if (key < 0) {
        PyErr_SetString(PyExc_ValueError,
                "key must be IPC_PRIVATE or a positive int");
        return NULL;
    }

    if ((qid = msgget(key, msgflag)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)qid);
}

static char *msgsnd_kwargs[] = {"id", "msg", "priority", "flags", NULL};

static PyObject *
python_msgsnd(PyObject *module, PyObject *args, PyObject *kwargs) {
    int mqid, length, flag;
    long prio;
    char *data;

    if (!PyArg_ParseTupleAndKeywords(
                args, kwargs, "is#l|i", msgsnd_kwargs,
                &mqid, &data, &length, &prio, &flag))
        return NULL;

    char buf[sizeof(msgbuf) + length - 1];
    msgbuf *mbufp = (msgbuf *)&buf[0];
    mbufp->type = prio;
    memcpy((char *)&mbufp->text[0], data, length);

    if (msgsnd(mqid, mbufp, length, flag) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *msgrcv_kwargs[] = {"id", "priority", "flags", "maxsize", NULL};

static PyObject *
python_msgrcv(PyObject *module, PyObject *args, PyObject *kwargs) {
    int mqid, flag;
    long prio, maxsize = pagesize;
    ssize_t recvd;
    PyObject *result, *str, *pyprio;

    if (!PyArg_ParseTupleAndKeywords(
                args, kwargs, "il|il", msgrcv_kwargs,
                &mqid, &prio, &flag, &maxsize))
        return NULL;

    char buf[sizeof(long) + maxsize];
    msgbuf *mbufp = (msgbuf *)&buf[0];

    if ((recvd = msgrcv(mqid, mbufp, maxsize, prio, flag)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (NULL == (pyprio = PyInt_FromLong(mbufp->type)))
        return NULL;

    if (NULL == (str = PyString_FromStringAndSize(&mbufp->text[0], recvd))) {
        Py_DECREF(pyprio);
        return NULL;
    }

    if (NULL == (result = PyTuple_New(2))) {
        Py_DECREF(pyprio);
        Py_DECREF(str);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 0, pyprio);
    PyTuple_SET_ITEM(result, 1, str);
    return result;
}

static char *semget_kwargs[] = {"key", "nsems", "flags", NULL};

static PyObject *
python_semget(PyObject *module, PyObject *args, PyObject *kwargs) {
    key_t key = 0;
    int semid, nsems = 0, semflag = 0;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "l|ii", semget_kwargs,
                (long *)&key, &nsems, &semflag))
        return NULL;

    if (semflag & IPC_CREAT && nsems == 0) {
        PyErr_SetString(PyExc_ValueError,
                "nsems must be provided and positive for IPC_CREAT");
        return NULL;
    }

    if (key < 0) {
        PyErr_SetString(PyExc_ValueError,
                "key must be IPC_PRIVATE or a positive int");
        return NULL;
    }

    if ((semid = semget(key, nsems, semflag)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return PyInt_FromLong((long)semid);
}

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

static char *semctl_kwargs[] = {"id", "cmd", "semnum", "arg", NULL};

static PyObject *
python_semctl(PyObject *self, PyObject *args, PyObject *kwargs) {
    int result, semid, cmd, semnum = -1;
    unsigned long nsems;
    PyObject *info = NULL;
    struct semid_ds sds;
    union semun sun;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|iO", semctl_kwargs,
                &semid, &cmd, &semnum, &info))
        return NULL;

    if (cmd == IPC_SET && unpythonify_seminfo(info, &sds))
        return NULL;

    if (cmd == IPC_SET || cmd == IPC_STAT)
        sun.buf = &sds;

    if (cmd == GETALL || cmd == SETALL) {
        /* have to stat to get nsems */
        sun.buf = &sds;
        if (semctl(semid, semnum, IPC_STAT, sun) < 0) {
            PyErr_SetFromErrno(PyExc_OSError);
            return NULL;
        }
        nsems = sds.sem_nsems;

        /* allocate storage for the array and set it */
        sun.array = malloc(sizeof(unsigned short) * nsems);
    }

    if (cmd == SETALL) {
        if (unpythonify_semarray(info, sun.array) < 0)
            return NULL;
    }

    if (cmd == GETNCNT
            || cmd == GETPID
            || cmd == GETVAL
            || cmd == GETZCNT
            || cmd == SETVAL) {
        if (semnum < 0) {
            PyErr_SetString(PyExc_ValueError,
                    "need a semnum > 0 for this cmd");
        }
    }

    if (cmd == SETVAL) {
        if (-1 == (sun.val = PyInt_AsLong(info)) && PyErr_Occurred())
            return NULL;
    }

    if ((result = semctl(semid, semnum, cmd, sun)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (cmd == IPC_STAT)
        return pythonify_sds(&sds);

    if (cmd == GETALL) {
        info = pythonify_semarray(sun.array, nsems);
        free(sun.array);
        return info;
    }

    if (cmd == SETALL) {
        free(sun.array);
    }

    if (cmd == GETNCNT
            || cmd == GETPID
            || cmd == GETVAL
            || cmd == GETZCNT) {
        return PyInt_FromLong((long)result);
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static char *semop_kwargs[] = {"id", "operations", "timeout", NULL};

static PyObject *
python_semop(PyObject *module, PyObject *args, PyObject *kwargs) {
    int semid;
    PyObject *pyops;
    long len;
    double dtimeout = -1;
    struct timespec timeout;
    struct timespec *timeoutp = &timeout;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "iO|d", semop_kwargs,
                &semid, &pyops, &dtimeout))
        return NULL;

    if ((len = PyObject_Length(pyops)) < 0) {
        PyErr_SetString(PyExc_TypeError, "semops must have a length");
        return NULL;
    }

    struct sembuf bufs[len];
    semops ops = {(unsigned int)len, &bufs[0]};
    if (unpythonify_semops(pyops, &ops) < 0) return NULL;

    if (dtimeout > 0) {
        if (timespec_ify(dtimeout, timeoutp)) {
            PyErr_SetString(PyExc_ValueError, "issue with timeout float");
            return NULL;
        }
    } else
        timeoutp = NULL;

    if (semtimedop(semid, ops.ops, (unsigned int)len, timeoutp) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * module methods struct
 */
static PyMethodDef module_methods[] = {
    {"ftok", python_ftok, METH_VARARGS,
        "generate an IPC key\n\
\n\
:param str filepath: string path of a readable file\n\
\n\
:param int id: integer for use in the key generation\n\
\n\
:returns: the integer key\n\
\n\
for the same filepath and integer id, this function should always return the\n\
same key.\n\
"},
    {"msgget", (PyCFunction)python_msgget, METH_VARARGS | METH_KEYWORDS,
        "get and/or create a message queue\n\
\n\
:param int key: key identifier for the message queue\n\
\n\
:param int flags:\n\
    (optional) bitwise ORed flags, and permissions in the low 9 bits if the\n\
    call creates a new message queue. supported flags are:\n\
\n\
    ``IPC_CREAT``:\n\
        create the message queue if one doesn't already exist for the key\n\
    ``IPC_EXCL``:\n\
        only for use with ``IPC_CREAT``, causes the function to fail with\n\
        ``EEXIST`` if a queue already exists for the key.\n\
\n\
:returns: a system-wide unique integer identifier for the new message queue\n\
"},
    {"msgctl", (PyCFunction)python_msgctl, METH_VARARGS | METH_KEYWORDS,
        "perform a control operation on a message queue's metadata\n\
\n\
:param int id:\n\
    unique identifier of the message queue (as returned by :func:`msgget`)\n\
\n\
:param int cmd:\n\
    a constant to specify which operation to perform. valid values are:\n\
\n\
    ``IPC_STAT``:\n\
        retrieve the meta information about the message queue\n\
    ``IPC_SET``:\n\
        set some fields of the message queue's metadata\n\
    ``IPC_RMID``:\n\
        remove the queue entirely\n\
\n\
:param setinfo:\n\
    optional (only for the ``IPC_SET`` cmd) data to set on the message\n\
    queue's metadata. :class:`msg_info<penguin.structs.msg_info>`\n\
    is a namedtuple class with the following fields (all required):\n\
\n\
    msg_qbytes:\n\
        the total capacity for all messages in the queue\n\
    perm_uid:\n\
        the user id of the queue's owning user\n\
    perm_gid:\n\
        the group id of the queue's owning group\n\
    perm_mode:\n\
        the integer permission bits (execute bits do nothing)\n\
:type setinfo: :class:`msg_info<penguin.structs.msg_info>`\n\
\n\
:returns:\n\
    ``None``, unless ``cmd`` was ``IPC_STAT``, in which case a\n\
    :class:`msqid_ds<penguin.structs.msqid_ds>` struct is returned. It is a\n\
    namedtuple class with the fields as ``struct msqid_ds`` as described in\n\
    the msgctl(2) manual.\n\
"},
    {"msgsnd", (PyCFunction)python_msgsnd, METH_VARARGS | METH_KEYWORDS,
        "send a message to a queue\n\
\n\
:param int id:\n\
    unique identifier of the message queue (as returned by :func:`msgget`)\n\
\n\
:param str msg: the message to place on the queue\n\
\n\
:param int priority:\n\
    the positive integer priority of the message (lower numbers are higher\n\
    priority). see the :func:`msgrcv` docstring for its relevance.\n\
\n\
:param int flags:\n\
    flags to apply to the message send. the only one recognized is\n\
    ``IPC_NOWAIT``, which makes the send fail with ``EAGAIN`` if the queue\n\
    is already too full, instead of the default blocking behavior.\n\
"},
    {"msgrcv", (PyCFunction)python_msgrcv, METH_VARARGS | METH_KEYWORDS,
        "receive a message from a queue\n\
\n\
:param int id:\n\
    unique identifier of the message queue (as returned by :func:`msgget`)\n\
\n\
:param int priority:\n\
    specifier for the message priorities to receive. with zero, we receive\n\
    the oldest message on the queue, regardless of priorities. with a\n\
    positive number, receive the oldest message with *exactly* the given\n\
    priority. with a negative number, receive the message with the lowest\n\
    priority which is less than the absolute value of the given int.\n\
\n\
    so given -5, we could receive any message with priorities from 1 to 5\n\
    inclusive, but preference would be given to lower priority values even\n\
    if they were placed on the queue later.\n\
\n\
:param int flags:\n\
    optional bitwise ORed flags modifying msgrcv's behavior:\n\
\n\
    ``IPC_NOWAIT``:\n\
        instead of blocking if no eligible messages are ready on the queue,\n\
        fail immediately with ``EAGAIN``.\n\
    ``MSG_EXCEPT``:\n\
        used with priority > 0, causes the message selection to pick the\n\
        first message with priority *other than* the given one.\n\
    ``MSG_NOERROR``:\n\
        if ``maxsize`` is smaller than the size of the actual message, this\n\
        flag causes the message to be received anyway and truncated (normal\n\
        behavior is failure with ``E2BIG``).\n\
\n\
:param int maxsize:\n\
    optional maximum length of the message to be received. a buffer of this\n\
    size will be briefly allocated regardless of the actual message size.\n\
    the default is set to the virtual memory page size, usually 4096 on\n\
    linux, but available as ``PAGESIZE``.\n\
\n\
:returns:\n\
    a two-tuple of the priority the message was submitted with and the\n\
    message string itself.\n\
"},
    {"semget", (PyCFunction)python_semget, METH_VARARGS,
        "get and/or create a semaphore set\n\
\n\
:param int key: key identifier for the semaphore set\n\
\n\
:param int nsems:\n\
    (optional) number of semaphores in the set if creating. if ``IPC_CREAT``\n\
    is included in the flags, a positive int for this argument is required.\n\
\n\
:param int flags:\n\
    (optional) bitwise ORed flags, and permissions in the low 9 bits if the\n\
    call can create a new semaphore set. supported flags are:\n\
\n\
    ``IPC_CREAT``:\n\
        create the semaphore set if one doesn't already exist for the key\n\
    ``IPC_EXCL``:\n\
        only for use with ``IPC_CREAT``, causes the function to fail with\n\
        ``EEXIST`` if a semaphore set already exists for the key.\n\
\n\
:returns: a system-wide unique integer identifier for the new semaphore set\n\
"},
    {"semctl", (PyCFunction)python_semctl, METH_VARARGS | METH_KEYWORDS,
        "run a semaphore set control operation\n\
\n\
:param int id:\n\
    identifier for the semaphore set on which to operate (as returned by\n\
    :func:`semget`)\n\
\n\
:param int cmd:\n\
    a constant to specify which opreation to perform. valid values are:\n\
\n\
    ``IPC_STAT``:\n\
        retrieve the metadata about the semaphore set, in the form of a\n\
        :class:`semid_ds<penguin.structs.semid_ds>` instance\n\
    ``IPC_SET``:\n\
        set some fields of the semaphore set's metadata\n\
    ``IPC_RMID``:\n\
        remove the semaphore set entirely\n\
    ``GETALL``:\n\
        return a list of the values of all semaphores in the set\n\
    ``SETALL``:\n\
        set the value for all semaphores in the set\n\
    ``GETZCNT``:\n\
        return the number of waiters waiting for the ``semnum``-th semaphore\n\
        to become zero\n\
    ``GETNCNT``:\n\
        return the number of waiters waiting for the ``semnum``-th\n\
        semaphore's value to increase\n\
    ``GETVAL``:\n\
        return the current value of the ``semnum``-th semaphore\n\
    ``SETVAL``:\n\
        set the value of the ``semnum``-th semaphore\n\
    ``GETPID``:\n\
        return the pid of the last process to :func:`semop` the\n\
        ``semnum``-th semaphore\n\
\n\
:param int semnum:\n\
    the zero-based index of the single semaphore on which to operate\n\
    (optional and in fact ignored unless ``cmd`` is ``GETZCNT``,\n\
    ``GETNCNT``, ``GETVAL``, ``SETVAL``, or ``GETPID``).\n\
\n\
:param arg:\n\
    an extra argument for those ``cmd``'s that need it:\n\
\n\
    ``IPC_SET``: should be a :class:`sem_info<penguin.structs.sem_info`\n\
    ``SETALL``: should be a list of ints\n\
    ``SETVAL``: should be an integer\n\
\n\
:returns:\n\
    for ``IPC_STAT`` and ``GET*`` commands, returns the answer to the query\n\
    (notably in the case of ``IPC_STAT`` it is a\n\
     :class:`semid_ds<penguin.structs.semid_ds>` instance). otherwise\n\
    ``None``.\n\
"},
    {"semop", (PyCFunction)python_semop, METH_VARARGS,
        "run a group of semaphore operations\n\
this function actually uses semtimedop(2), to power the timeout\n\
\n\
:param int id:\n\
    identifier of the semaphore set on which to operate (as returned by\n\
    :func:`semget`)\n\
\n\
:param list operations:\n\
    the operations to perform. each item in this list should be a tuple\n\
    containing 3 ints:\n\
\n\
    sem_num:\n\
        the zero-based index of the semaphore in the set on which to\n\
        operate\n\
    sem_op:\n\
        zero means block until the semaphore's value is 0, a positive number\n\
        is added to the semaphore's value immediately, a negative number\n\
        will be subtracted from the semaphore's value, possibly blocking\n\
        until the subtraction wouldn't result in a negative number.\n\
    sem_flg:\n\
        flags applied to this operation. the only two recognized are\n\
        ``IPC_NOWAIT`` which causes any action that would block to instead\n\
        immediately fail with ``EAGAIN``, and ``SEM_UNDO``, which causes the\n\
        operation to automatically be undone when this process terminates.\n\
\n\
    there is a ``collections.namedtuple`` class provided for these\n\
    operations, :class:`sembuf<penguin.structs.sembuf>`, but it's not\n\
    strictly required.\n\
\n\
:param (int or float) timeout:\n\
    optional maximum time to block before failing the operations and raising\n\
    with ``EAGAIN``. the default of -1 means no limit.\n\
"},
    {NULL, NULL, 0, NULL}
};


#if PY_MAJOR_VERSION >= 3

static struct PyModuleDef sysvipc_module = {
    PyModuleDef_HEAD_INIT,
    "penguin.sysvipc",
    "",
    -1, module_methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_sysvipc(void) {
    PyObject *module = PyModule_Create(&sysvipc_module);

#else

PyMODINIT_FUNC
initsysvipc(void) {
    PyObject *module = Py_InitModule("penguin.sysvipc", module_methods);

#endif

    pagesize = getpagesize();

    PyModule_AddIntConstant(module, "PAGESIZE", pagesize);
    PyModule_AddIntConstant(module, "IPC_CREAT", IPC_CREAT);
    PyModule_AddIntConstant(module, "IPC_EXCL", IPC_EXCL);
    PyModule_AddIntConstant(module, "IPC_NOWAIT", IPC_NOWAIT);
    PyModule_AddIntConstant(module, "IPC_PRIVATE", IPC_PRIVATE);
    PyModule_AddIntConstant(module, "IPC_STAT", IPC_STAT);
    PyModule_AddIntConstant(module, "IPC_SET", IPC_SET);
    PyModule_AddIntConstant(module, "IPC_RMID", IPC_RMID);

    PyModule_AddIntConstant(module, "MSG_EXCEPT", MSG_EXCEPT);
    PyModule_AddIntConstant(module, "MSG_NOERROR", MSG_NOERROR);

    PyModule_AddIntConstant(module, "SEM_UNDO", SEM_UNDO);
    PyModule_AddIntConstant(module, "GETALL", GETALL);
    PyModule_AddIntConstant(module, "SETALL", SETALL);
    PyModule_AddIntConstant(module, "GETNCNT", GETNCNT);
    PyModule_AddIntConstant(module, "GETPID", GETPID);
    PyModule_AddIntConstant(module, "GETVAL", GETVAL);
    PyModule_AddIntConstant(module, "GETZCNT", GETZCNT);
    PyModule_AddIntConstant(module, "SETVAL", SETVAL);

    PyObject *structs = PyImport_ImportModule("penguin.structs");

    if (NULL != structs && PyObject_HasAttrString(structs, "ipc_perm"))
        PyIpcPerm = PyObject_GetAttrString(structs, "ipc_perm");

    if (NULL != structs && PyObject_HasAttrString(structs, "msqid_ds"))
        PyMsqidDs = PyObject_GetAttrString(structs, "msqid_ds");

    if (NULL != structs && PyObject_HasAttrString(structs, "semid_ds"))
        PySemidDs = PyObject_GetAttrString(structs, "semid_ds");

    if (NULL == structs)
        PyErr_Clear();

#if PY_MAJOR_VERSION >= 3
    return module;
#endif

}
