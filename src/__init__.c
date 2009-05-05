#include "__init__.h"
#include "fcgiproto.h"

PyObject *fcgiev_module;

/**
  Buffer.
  note: in Py2.6 we have PyByteArray which is faster than PyBytes
**/
typedef struct {
  char *p;
  PyObject *buf;
} buf_t;

typedef struct {
  buf_t b;
  PyObject *fd;
  PyObject *recvfunc;
  PyObject *sendfunc;
  PyObject *spawner;
  PyObject *trampoline;
  PyObject *recvargs;
  PyObject *sendargs;
} ctx_t;

#define buf_size(b) PyBytes_GET_SIZE((b)->buf)
#define buf_length(b) ((b)->p - PyBytes_AS_STRING((b)->buf))
#define buf_data(b) PyBytes_AS_STRING((b)->buf)


inline static int buf_append(buf_t *b, const char *data, Py_ssize_t len) {
  Py_ssize_t newsize = buf_length(b) + len;
  if (buf_size(b) < newsize) {
    newsize = FE_ALIGN_M(newsize);
    if (_PyString_Resize(&b->buf, newsize) == -1)
      return 0;
  }
  memcpy((void *)b->p, (const void *)data, (size_t)len);
  b->p += len;
  return 1;
}


inline static void buf_drain(buf_t *b, Py_ssize_t amount) {
  assert(amount <= buf_size(b));
  b->p -= amount;
}


inline static int _recv(ctx_t *ctx, Py_ssize_t minlen) {
  PyObject *bytes = NULL, *tr;
  PyObject *ptype = NULL, *pvalue = NULL, *ptraceback = NULL;
  PyObject *args = PyTuple_Pack(1, NUMBER_FromLong((long)minlen));
  int ret = 0;
  
  while(1) {
    bytes = PyObject_CallObject(ctx->recvfunc, args);
    
    if (bytes == NULL) {
      PyErr_Fetch(&ptype, &pvalue, &ptraceback);
      // todo check if ptype/pvalue is socket.error
      if (PyTuple_GET_SIZE(((PyBaseExceptionObject *)pvalue)->args) > 0) {
        #define ERN ((PyBaseExceptionObject *)pvalue)->args
        if (ERN && NUMBER_Check(ERN) && NUMBER_AsLong(ERN) == 35) {
          // EWOULDBLOCK / temp unavil. -- jump on trampoline
          if ((tr = PyObject_CallObject(ctx->trampoline, ctx->recvargs)) == NULL) {
            ret = -1;
            break;
          }
          Py_DECREF(tr);
          continue;
        }
        #undef ERN
      }
      ret = -1;
      break;
    }
    
    assert(bytes != Py_None);
    
    if (PyBytes_GET_SIZE(bytes) == 0) {
      ret = 1;
      break;
    }
    
    if (!buf_append(&ctx->b, PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes))) {
      // _PyString_Resize failed (out of memory)
      ret = -1;
      break;
    }
    
    if (buf_length(&ctx->b) >= minlen)
      break;
  }
  
  Py_DECREF(args);
  Py_XDECREF(bytes);
  return ret;
}


inline static int _send(ctx_t *ctx, const char *pch, Py_ssize_t len) {
  PyObject *data, *args, *sent, *tr;
  PyObject *ptype = NULL, *pvalue = NULL, *ptraceback = NULL;
  int ret;
  
  ret = 0;
  data = PyBytes_FromStringAndSize(pch, len);
  args = PyTuple_Pack(1, data);
  
  while (1) {
    
    if ((sent = PyObject_CallObject(ctx->sendfunc, args)) == NULL) {
      PyErr_Fetch(&ptype, &pvalue, &ptraceback);
      // todo check if ptype/pvalue is socket.error
      if (PyTuple_GET_SIZE(((PyBaseExceptionObject *)pvalue)->args) > 0) {
        #define ERN ((PyBaseExceptionObject *)pvalue)->args
        if (ERN && NUMBER_Check(ERN) && NUMBER_AsLong(ERN) == 35) {
          // EWOULDBLOCK / temp unavil. -- jump on trampoline
          if ((tr = PyObject_CallObject(ctx->trampoline, ctx->sendargs)) == NULL) {
            ret = -1;
            break;
          }
          Py_DECREF(tr);
          continue;
        }
        #undef ERN
      }
      // actual exception
      goto returnnow;
    }
    
    assert(sent != Py_None);
    assert(NUMBER_Check(sent));
    
    if (NUMBER_AsLong(sent) >= len)
      break;
  }
  
  // clean return
  ret = 1;
  
returnnow:
  Py_DECREF(args);
  Py_DECREF(data);
  return ret;
}


// -----------------------------------------------------------------


inline static int process_begin_request(ctx_t *ctx, uint16_t id, const begin_request_t *br) {
  log_debug("begin request { id: %d, keepconn: %d, role: %d }", id,
    (br->flags & FLAG_KEEP_CONN) == 1, ((br->roleB1 << 8) + br->roleB0) );
  buf_drain(&ctx->b, sizeof(header_t)+sizeof(begin_request_t));
  return 1;
}


inline static int process_unknown(ctx_t *ctx, uint8_t type, uint16_t len) {
  log_debug("received unknown record of type %d\n", type);
  unknown_type_t msg;
  unknown_type_init(&msg, type);
  if (!_send(ctx, (const void *)&msg, sizeof(unknown_type_t)))
    return 0;
  buf_drain(&ctx->b, sizeof(header_t) + len);
  return 1;
}


/**
 * fcgiev.process(OOO)
 */
PyObject *fcgiev_process(PyObject *_null, PyObject *args, PyObject *kwargs) {
  ctx_t ctx;
  PyObject *retval = NULL;
  
  static char *kwlist[] = {"fd", "spawner", "trampoline", NULL};
  
  /* Parse arguments */
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOO:process", kwlist, &ctx.fd, &ctx.spawner, &ctx.trampoline)) {
    return NULL;
  }
  
  /* Get fd functions */
  {
    if ((ctx.recvfunc = PyObject_GetAttrString(ctx.fd, "recv")) == NULL)
      return NULL;
    Py_DECREF(ctx.recvfunc);
    if (ctx.recvfunc == Py_None) {
      // we can not use PyMethod_Check because recv might be a built-in method
      return PyErr_Format(PyExc_TypeError, "fd.recv must be a method");
    }
    if ((ctx.sendfunc = PyObject_GetAttrString(ctx.fd, "send")) == NULL)
      return NULL;
    Py_DECREF(ctx.sendfunc);
    if (ctx.sendfunc == Py_None) {
      return PyErr_Format(PyExc_TypeError, "fd.send must be a method");
    }
  }
  
  /* Typecheck trampoline */
  if (!PyFunction_Check(ctx.trampoline)) {
    PyErr_Format(PyExc_TypeError, "trampoline is not a function");
    return NULL;
  }
  
  /* Arguments used often */
  ctx.recvargs = Py_BuildValue("(OO)", ctx.fd, Py_True);
  ctx.sendargs = Py_BuildValue("(OOO)", ctx.fd, Py_None, Py_True);
  
  /* process fastcgi packets */
  const header_t *hp;
  int readst;
  ctx.b.buf = PyBytes_FromStringAndSize(NULL, 32);
  ctx.b.p = PyBytes_AS_STRING(ctx.b.buf);
  uint16_t msg_len, msg_id;
  Py_ssize_t lenreq;
  
  #define CHECKREAD do { \
    if (readst != 0) { \
      Py_XDECREF(ctx.b.buf); \
      if (readst == -1) \
        goto returnnow; \
      break; \
    } \
  } while(0)
  
  /*#define READ(t, n) readst = _read(fd, recvfunc, &b, t, n) */
  
  while(1) {
    readst = _recv(&ctx, 8);
    CHECKREAD;
    
    // cast
    hp = (const header_t *)PyBytes_AS_STRING(ctx.b.buf);
    
    // We only handle FCGI v1
    if (hp->version != 1) {
      PyErr_Format(PyExc_IOError, 
        "cannot handle FastCGI protocol version %u", hp->version);
      goto returnnow;
    }
    
    // Check whether we have the whole message that follows the
    // headers in our buffer already. If not, we can't process it
    // yet.
    msg_len = (hp->contentLengthB1 << 8) + hp->contentLengthB0;
    msg_id  = (hp->requestIdB1 << 8) + hp->requestIdB0;
    
    // Need more data?
    if (hp->type == TYPE_PARAMS || hp->type == TYPE_STDIN || hp->type == TYPE_BEGIN_REQUEST) {
      lenreq = (sizeof(header_t) + msg_len + hp->paddingLength) - buf_length(&ctx.b);
      if (lenreq > 0) {
        readst = _recv(&ctx, lenreq);
        CHECKREAD;
      }
    }
    
    // Process the message.
    printf("fcgiproto>> received message: id: %d, bodylen: %d, padding: %d, type: %d\n",
      msg_id, msg_len, hp->paddingLength, (int)hp->type);
    
    switch (hp->type) {
      case TYPE_BEGIN_REQUEST:
        if (!process_begin_request(&ctx, msg_id, (const begin_request_t *)(hp + sizeof(header_t)) )) {
          goto returnnow;
        }
        break;
      //case TYPE_ABORT_REQUEST:
      //  process_abort_request(&b, msg_id);
      //  break;
      //case TYPE_PARAMS:
      //  process_params(&b, msg_id, (const uint8_t *)PyBytes_AS_STRING(b.buf) + sizeof(header_t), msg_len);
      //  break;
      //case TYPE_STDIN:
      //  process_stdin(&b, msg_id, (const uint8_t *)PyBytes_AS_STRING(b.buf) + sizeof(header_t), msg_len);
      //  break;
      
      //case TYPE_END_REQUEST:
      //case TYPE_STDOUT:
      //case TYPE_STDERR:
      //case TYPE_DATA:
      //case TYPE_GET_VALUES:
      //case TYPE_GET_VALUES_RESULT:
      //case TYPE_UNKNOWN:
      default:
        if (!process_unknown(&ctx, hp->type, msg_len)) {
          goto returnnow;
        }
    }
  }
  
  // set clean return
  retval = Py_None;
  Py_INCREF(retval);

returnnow:
  Py_XDECREF(ctx.recvargs);
  Py_XDECREF(ctx.sendargs);
  Py_XDECREF(ctx.b.buf);
  return retval;
}


/*
 * Module functions
 */
static PyMethodDef fcgiev_functions[] = {
  {"process", (PyCFunction)fcgiev_process, METH_VARARGS|METH_KEYWORDS, NULL},
  {NULL, NULL, 0, NULL}
};


/*
 * Module structure (Only used in Python >=3.0)
 */
#if (PY_VERSION_HEX >= 0x03000000)
  static struct PyModuleDef fcgiev_module_t = {
    PyModuleDef_HEAD_INIT,
    "_fcgiev",   /* Name of module */
    NULL,    /* module documentation, may be NULL */
    -1,      /* size of per-interpreter state of the module,
                or -1 if the module keeps state in global variables. */
    fcgiev_functions,
    NULL,   /* Reload */
    NULL,   /* Traverse */
    NULL,   /* Clear */
    NULL    /* Free */
  };
#endif


/*
 * Module initialization
 */
#if (PY_VERSION_HEX < 0x03000000)
DL_EXPORT(void) init_fcgiev(void)
#else
PyMODINIT_FUNC  PyInit__fcgiev(void)
#endif
{
  /* Create module */
  #if (PY_VERSION_HEX < 0x03000000)
    fcgiev_module = Py_InitModule("_fcgiev", fcgiev_functions);
  #else
    fcgiev_module = PyModule_Create(&fcgiev_module_t);
  #endif
  if (fcgiev_module == NULL)
    goto exit;
  
  /* Create exceptions here if needed */
  
  /* Register types */
  #define R(name, okstmt) \
    if (name(fcgiev_module) okstmt) { \
      log_error("sub-component initializer '" #name "' failed"); \
      goto exit; \
    }
  /* Example: R(tc_SomeClass_register, != 0) */
  #undef R
  
  /* Register int constants */
  #define ADD_INT(NAME) PyModule_AddIntConstant(fcgiev_module, #NAME, NAME)
  /* Example: ADD_INT(TCESUCCESS); */
  #undef ADD_INT
  /* end adding constants */

exit:
  if (PyErr_Occurred()) {
    PyErr_Print();
    PyErr_SetString(PyExc_ImportError, "can't initialize module _fcgiev");
    Py_XDECREF(fcgiev_module);
    fcgiev_module = NULL;
  }
  
  #if (PY_VERSION_HEX < 0x03000000)
    return;
  #else
    return fcgiev_module;
  #endif
}
