#ifndef FCGIEV___INIT___H
#define FCGIEV___INIT___H

#include "_macros.h"

extern PyObject *fcgiev_module;

/**
 * @param PyObject *sock
 * @param PyObject *spawner
 */
PyObject *fcgiev_process(PyObject *, PyObject *args, PyObject *kwargs);

#endif
