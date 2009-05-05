#include "__init__.h"
#include "fcgiproto.h"
#include "buffer.h"

#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>

PyObject *fcgiev_module;


// ----------------------------------------------------------

typedef struct {
  int fd;
  buf_t *buf;
  PyObject *recvfunc;
  PyObject *sendfunc;
  PyObject *spawner;
  PyObject *trampoline;
  PyObject *trampolinerecvargs;
  PyObject *trampolinesendargs;
} ctx_t;


#define MAX_READ 4096

inline static int _read(ctx_t *ctx, Py_ssize_t length) {
  PyObject *tr;
  int ret = 0, n = MAX_READ;
  ssize_t received;
  void *ptr;
  #ifdef FIONREAD
    int r;
  #endif
  
  buf_reserve(ctx->buf, BUF_LENGTH(ctx->buf) + (size_t)length);
  
  ptr = BUF_DATA(ctx->buf);
  
  while(1) {
    
    /* use FIONREAD to get exact number of bytes waiting in the socket buffer */
    #ifdef FIONREAD
      r = ioctl(ctx->fd, FIONREAD, &n);
      if (r == -1 || n == 0) {
        if (r == -1 && errno && errno != EAGAIN) {
          PyErr_SetFromErrno(PyExc_IOError);
          ret = -1;
          break;
        }
        //log_debug("r: %d, errno: %d (%d)", r, errno, EAGAIN);
        tr = PyObject_CallObject(ctx->trampoline, ctx->trampolinerecvargs);
        if (tr == NULL) {
          ret = -1;
          break;
        }
        Py_DECREF(tr);
        continue;
      }
    #endif
    
    buf_reserve(ctx->buf, BUF_LENGTH(ctx->buf) + (size_t)n);
    
    //log_debug("read(%d, %p, %d)", ctx->fd, BUF_DATA(ctx->buf), n);
    received = recv(ctx->fd, BUF_DATA(ctx->buf), n, 0);
    
    if (received < 1) {
      #ifndef FIONREAD
        if (errno == EAGAIN) {
          tr = PyObject_CallObject(ctx->trampoline, ctx->trampolinerecvargs);
          if (tr == NULL) {
            ret = -1;
            break;
          }
          Py_DECREF(tr);
          continue;
        }
      #endif
      ret = -1;
      if (errno != 0) {
        log_debug("socket errno [%d] %s", errno, strerror(errno));
        PyErr_SetFromErrno(PyExc_IOError);
      }
      else if (received == 0) {
        log_debug("EOF");
        ret = 1; // EOF
      }
      else {
        assert("should never get here" == NULL);
      }
      break;
    }
    
    ctx->buf->off += received;
    
    if ( BUF_LENGTH(ctx->buf) >= length )
      break;
  }
  
  return ret;
}


inline static int _write(ctx_t *ctx, const void *buffer, ssize_t length) {
  PyObject *tr;
  int ret = 1;
  ssize_t sent;
  
  while (1) {
    sent = send(ctx->fd, buffer, (size_t)length, 0);
    
    if (sent == -1) {
      if (errno == EAGAIN) {
        if ((tr = PyObject_CallObject(ctx->trampoline, ctx->trampolinesendargs)) != NULL) {
          Py_DECREF(tr);
          continue;
        }
        // PyObject_CallObject failed and exc is set
      }
      else {
        log_debug("socket errno [%d] %s", errno, strerror(errno));
        PyErr_SetFromErrno(PyExc_IOError);
      }
      ret = -1;
      break;
    }
    
    if (sent == 0) {
      ret = 0; // EOF
      break;
    }
    
    length -= sent;
    
    if (length <= 0)
      break;
  }
  
  return ret;
}


// -----------------------------------------------------------------


inline static int process_begin_request(ctx_t *ctx, uint16_t id, const begin_request_t *br) {
  log_debug("begin request { id: %d, keepconn: %d, role: %d }", id,
    (br->flags & FLAG_KEEP_CONN) == 1, (uint16_t)((br->roleB1 << 8) + br->roleB0) );
  
  if ( (uint16_t)((br->roleB1 << 8) + br->roleB0) != ROLE_RESPONDER ) {
    PyErr_Format( PyExc_IOError, 
      "unknown FastCGI application role %d -- can only handle RESPONDER",
      (uint16_t)((br->roleB1 << 8) + br->roleB0) );
    return 0;
  }
  
  buf_drain(ctx->buf, sizeof(header_t)+sizeof(begin_request_t));
  
  return 1;
}


inline static int process_unknown(ctx_t *ctx, uint8_t type, uint16_t len) {
  log_debug("received unknown record of type %d", type);
  unknown_type_t msg;
  unknown_type_init(&msg, type);
  
  if (_write(ctx, (const void *)&msg, sizeof(unknown_type_t)) == -1)
    return 0;
  
  buf_drain(ctx->buf, sizeof(header_t) + len);
  
  return 1;
}


/*inline static void _configure_socket(int fd) {
  assert(setsockopt(fd, SOL_SOCKET, int option_name, const void *option_value, socklen_t option_len) == 0);
  SO_RCVLOWAT
}*/


/**
 * fcgiev.process(OOO)
 */
PyObject *fcgiev_process(PyObject *_null, PyObject *args, PyObject *kwargs) {
  ctx_t *ctx;
  PyObject *retval = NULL, *pyfd = NULL, *pyfilenoret;
  
  static char *kwlist[] = {"fd", "spawner", "trampoline", NULL};
  ctx = (ctx_t *)calloc(1, sizeof(ctx_t));
  
  /* Parse arguments */
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOO:process", kwlist, 
    &pyfd, &ctx->spawner, &ctx->trampoline))
  {
    goto returnnow;
  }
  
  /* Get fd fileno */
  pyfilenoret = PyObject_CallMethod(pyfd, "fileno", NULL);
  if (pyfilenoret == NULL)
    goto returnnow;
  ctx->fd = NUMBER_AsLong(pyfilenoret);
  Py_DECREF(pyfilenoret);
  
  /* Typecheck trampoline */
  if (!PyFunction_Check(ctx->trampoline)) {
    PyErr_Format(PyExc_TypeError, "trampoline is not a function");
    goto returnnow;
  }
  
  /* Arguments used often */
  ctx->trampolinerecvargs = Py_BuildValue("(OO)", pyfd, Py_True);
  ctx->trampolinesendargs = Py_BuildValue("(OOO)", pyfd, Py_None, Py_True);
  
  /* process fastcgi packets */
  const header_t *hp;
  int readst;
  ctx->buf = buf_new();
  uint16_t msg_len, msg_id;
  Py_ssize_t lenreq;
  
  #define CHECKREAD do { \
    if (readst != 0) { \
      if (readst == -1) \
        goto returnnow; \
      break; \
    } \
  } while(0)
  
  
  while(1) {
    if (BUF_LENGTH(ctx->buf) < sizeof(header_t)) {
      readst = _read(ctx, sizeof(header_t));
      CHECKREAD;
    }
    
    //DUMPBYTES(BUF_DATA(ctx->buf), BUF_LENGTH(ctx->buf));
    
    // header pointer
    hp = (const header_t *)BUF_DATA(ctx->buf);
    
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
    lenreq = (sizeof(header_t) + msg_len + hp->paddingLength) - BUF_LENGTH(ctx->buf);
    if (lenreq > 0) {
      readst = _read(ctx, lenreq);
      CHECKREAD;
    }
    
    // Process the message.
    printf("fcgiproto>> received message: id: %d, bodylen: %d, padding: %d, type: %d\n",
      msg_id, msg_len, hp->paddingLength, (int)hp->type);
    
    switch (hp->type) {
      case TYPE_BEGIN_REQUEST:
        if (!process_begin_request(ctx, msg_id, (const begin_request_t *)(BUF_DATA(ctx->buf) + sizeof(header_t)) )) {
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
        if (!process_unknown(ctx, hp->type, msg_len)) {
          goto returnnow;
        }
    }
  }
  
  // set clean return
  retval = Py_None;
  Py_INCREF(retval);

returnnow:
  if (ctx) {
    Py_XDECREF(ctx->trampolinerecvargs);
    Py_XDECREF(ctx->trampolinesendargs);
    if (ctx->buf != NULL)
      buf_free(ctx->buf);
    free(ctx);
    ctx = NULL;
  }
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
