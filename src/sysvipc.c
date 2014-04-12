#include "src/common.h"

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>


/* set these to penguin.structs.* at import time */
static PyObject *PyIpcPerm = NULL;
static PyObject *PyMsqidDs = NULL;

static PyObject *
pythonify_mqds(struct msqid_ds *mqds) {
    PyObject *obj, *args, *perm, *result;

    if (NULL == (args = PyTuple_New(7))) return NULL;

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.__key)))
        goto fail;
    PyTuple_SET_ITEM(args, 0, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.uid)))
        goto fail;
    PyTuple_SET_ITEM(args, 1, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.gid)))
        goto fail;
    PyTuple_SET_ITEM(args, 2, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.cuid)))
        goto fail;
    PyTuple_SET_ITEM(args, 3, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.cgid)))
        goto fail;
    PyTuple_SET_ITEM(args, 4, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.mode)))
        goto fail;
    PyTuple_SET_ITEM(args, 5, obj);

    if (NULL == (obj = PyInt_FromLong((long)mqds->msg_perm.__seq)))
        goto fail;
    PyTuple_SET_ITEM(args, 6, obj);

    perm = PyObject_Call(PyIpcPerm, args, NULL);
    Py_DECREF(args);
    if (NULL == perm) return NULL;

    if (NULL == (args = PyTuple_New(9))) {
        Py_DECREF(perm);
        return NULL;
    }
    PyTuple_SET_ITEM(args, 0, perm);

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

/*
 * python msgq object struct
 */
typedef struct {
    PyObject_HEAD
    int id;
} python_msgq_object;

typedef struct {
    long type;
    char text[1];
} msgbuf;

/*
 * python msgq object methods
 */
static PyObject *
python_msgq_send(PyObject *self, PyObject *args) {
    int mqid = ((python_msgq_object *)self)->id;
    long type;
    char *data;
    int length, flag;

    if (!PyArg_ParseTuple(args, "ls#i", &type, &data, &length, &flag))
        return NULL;

    char buf[sizeof(long) + length];
    msgbuf *mbufp = (msgbuf *)&buf[0];
    mbufp->type = type;
    memcpy((char *)&mbufp->text[0], data, length);

    if (msgsnd(mqid, mbufp, length, flag) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_msgq_recv(PyObject *self, PyObject *args) {
    int mqid = ((python_msgq_object *)self)->id;
    long type, maxsize;
    int flag;
    ssize_t recvd;
    PyObject *result, *str, *pytype;
    
    if (!PyArg_ParseTuple(args, "lil", &type, &flag, &maxsize))
        return NULL;

    char buf[sizeof(long) + maxsize];
    msgbuf *mbufp = (msgbuf *)&buf[0];

    if ((recvd = msgrcv(mqid, mbufp, maxsize, type, flag)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (NULL == (pytype = PyInt_FromLong(mbufp->type)))
        return NULL;

    if (NULL == (str = PyString_FromStringAndSize(&mbufp->text[0], recvd))) {
        Py_DECREF(pytype);
        return NULL;
    }

    if (NULL == (result = PyTuple_New(2))) {
        Py_DECREF(pytype);
        Py_DECREF(str);
        return NULL;
    }
    PyTuple_SET_ITEM(result, 0, pytype);
    PyTuple_SET_ITEM(result, 1, str);
    return result;
}

static PyObject *
python_msgq_stat(PyObject *self, PyObject *args) {
    int mqid = ((python_msgq_object *)self)->id;
    struct msqid_ds mqds;

    if (msgctl(mqid, IPC_STAT, &mqds) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    return pythonify_mqds(&mqds);
}

static PyObject *
python_msgq_set(PyObject *self, PyObject *args) {
    int mqid = ((python_msgq_object *)self)->id;
    struct msqid_ds mqds;
    unsigned long msg_qbytes;
    int uid, gid, mode;

    if (!PyArg_ParseTuple(args, "kiii", &msg_qbytes, &uid, &gid, &mode))
        return NULL;

    mqds.msg_qbytes = (msgqnum_t)msg_qbytes;
    mqds.msg_perm.uid = (uid_t)uid;
    mqds.msg_perm.gid = (gid_t)gid;
    mqds.msg_perm.mode = (unsigned short)mode;

    if (msgctl(mqid, IPC_SET, &mqds) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
python_msgq_remove(PyObject *self, PyObject *args) {
    int mqid = ((python_msgq_object *)self)->id;

    if (msgctl(mqid, IPC_RMID, NULL) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef msgq_methods[] = {
    {"send", python_msgq_send, METH_VARARGS,
        "send a message to the queue"},
    {"recv", python_msgq_recv, METH_VARARGS,
        "receive a message from the queue"},
    {"stat", python_msgq_stat, METH_NOARGS,
        "get information about the message queue"},
    {"set", python_msgq_set, METH_VARARGS,
        "set attributes and permissions of the message queue"},
    {"remove", python_msgq_remove, METH_NOARGS,
        "remove the message queue from the system"},
    {NULL, NULL, 0, NULL}
};


/*
 * python msgq type definition
 */

static PyMemberDef msgq_members[] = {
    {"id", T_INT, sizeof(PyObject), READONLY, "system-wide message queue id"}
};

static PyTypeObject python_msgq_type = {
    PyObject_HEAD_INIT(&PyType_Type)
#if PY_MAJOR_VERSION < 3
    0,                                         /* ob_size */
#endif
    "penguin.sysvipc.msgq",                    /* tp_name */
    sizeof(python_msgq_object),                /* tp_basicsize */
    0,                                         /* tp_itemsize */
    (destructor)PyObject_Del,                  /* tp_dealloc */
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
    msgq_methods,                              /* tp_methods */
    msgq_members,                              /* tp_members */
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

static PyObject *
python_msgget(PyObject *module, PyObject *args) {
    key_t key = 0;
    int msgflag, qid;
    python_msgq_object *msgq;

    if (!PyArg_ParseTuple(args, "li", (long *)&key, &msgflag))
        return NULL;

    if (key < 0) {
        PyErr_SetString(PyExc_ValueError,
                "key must be IPC_PRIVATE, a positive int, or a tuple");
        return NULL;
    }

    if ((qid = msgget(key, msgflag)) < 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    if (NULL == (msgq = PyObject_New(python_msgq_object, &python_msgq_type)))
        return NULL;
    msgq->id = qid;
    return (PyObject *)msgq;
}


/*
 * module methods struct
 */
static PyMethodDef module_methods[] = {
    {"ftok", python_ftok, METH_VARARGS,
        "generate an IPC key\n\
\n\
:param str filepath: string path of a readable file\n\
:param int id: integer for use in the key generation\n\
:returns: the integer key\n\
\n\
for the same filepath and integer id, this function should always return the\n\
same key.\n\
"},
    {"msgget", python_msgget, METH_VARARGS,
        "get a message queue\n\
\n\
:param key:\n\
    key specification for the message queue. this can be a two-tuple of\n\
    ``(filepath, proj_id)`` (see the ftok(3) man page) or the constant\n\
    ``IPC_PRIVATE``.\n\
:param int msgflg: bitwise ORed IPC_* flags (see the msgget(2) man page)\n\
:returns: a new msgq object instance\n\
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

    PyModule_AddIntConstant(module, "IPC_CREAT", IPC_CREAT);
    PyModule_AddIntConstant(module, "IPC_EXCL", IPC_EXCL);
    PyModule_AddIntConstant(module, "IPC_NOWAIT", IPC_NOWAIT);
    PyModule_AddIntConstant(module, "IPC_PRIVATE", IPC_PRIVATE);
    PyModule_AddIntConstant(module, "MSG_NOERROR", MSG_NOERROR);

    PyObject *structs = PyImport_ImportModule("penguin.structs");

    if (NULL != structs && PyObject_HasAttrString(structs, "ipc_perm"))
        PyIpcPerm = PyObject_GetAttrString(structs, "ipc_perm");

    if (NULL != structs && PyObject_HasAttrString(structs, "msqid_ds"))
        PyMsqidDs = PyObject_GetAttrString(structs, "msqid_ds");

    if (NULL == structs)
        PyErr_Clear();

    Py_INCREF(&python_msgq_type);
    PyType_Ready(&python_msgq_type);

#if PY_MAJOR_VERSION >= 3
    return module;
#endif

}
