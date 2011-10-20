#include <Python.h>
#include <structmember.h>

#if PY_MAJOR_VERSION >= 3
    #define PyInt_FromLong             PyLong_FromLong
    #define PyInt_AsLong               PyLong_AsLong
    #define PyNumber_Int               PyNumber_Long
    #define PyString_FromStringAndSize PyBytes_FromStringAndSize
#endif
