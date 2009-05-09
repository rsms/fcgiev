#ifndef _FCGIEV_FCGIPROTO_H_
#define _FCGIEV_FCGIPROTO_H_

#include <stdint.h>
#include <string.h> /* memset */

#define FCGI_LISTENSOCK_FILENO 0
#define FLAG_KEEP_CONN 1
#define FCGI_MAX_REQUESTS __SHRT_MAX__+__SHRT_MAX__

#define ROLE_RESPONDER  1
#define ROLE_AUTHORIZER 2
#define ROLE_FILTER     3

enum {
  TYPE_BEGIN_REQUEST     =  1,
  TYPE_ABORT_REQUEST     =  2,
  TYPE_END_REQUEST       =  3,
  TYPE_PARAMS            =  4,
  TYPE_STDIN             =  5,
  TYPE_STDOUT            =  6,
  TYPE_STDERR            =  7,
  TYPE_DATA              =  8,
  TYPE_GET_VALUES        =  9,
  TYPE_GET_VALUES_RESULT = 10,
  TYPE_UNKNOWN           = 11
};


enum {
  PROTOST_REQUEST_COMPLETE = 0,
  PROTOST_CANT_MPX_CONN    = 1,
  PROTOST_OVERLOADED       = 2,
  PROTOST_UNKNOWN_ROLE     = 3
};


typedef struct {
  uint8_t version;
  uint8_t type;
  uint8_t requestIdB1;
  uint8_t requestIdB0;
  uint8_t contentLengthB1;
  uint8_t contentLengthB0;
  uint8_t paddingLength;
  uint8_t reserved;
} header_t; // 8


typedef struct {
  uint8_t roleB1;
  uint8_t roleB0;
  uint8_t flags;
  uint8_t reserved[5];
} begin_request_t; // 8


typedef struct {
  header_t header;
  uint8_t appStatusB3;
  uint8_t appStatusB2;
  uint8_t appStatusB1;
  uint8_t appStatusB0;
  uint8_t protocolStatus;
  uint8_t reserved[3];
} end_request_t; // 16


typedef struct {
  header_t header;
  uint8_t type;
  uint8_t reserved[7];
} unknown_type_t; // 16


inline static void header_init(header_t *self, uint8_t t, uint16_t id, uint16_t len) {
  self->version = '\1';
  self->type = t;
  self->requestIdB1 = id >> 8;
  self->requestIdB0 = id & 0xff;
  self->contentLengthB1 = len >> 8;
  self->contentLengthB0 = len & 0xff;
  self->paddingLength = '\0';
  self->reserved = '\0';
}


inline static void end_request_init(end_request_t *self, uint16_t id, uint32_t ast, uint8_t protostatus) {
  header_init((header_t *)self, TYPE_END_REQUEST, id, sizeof(end_request_t)-sizeof(header_t));
  self->appStatusB3 = (ast >> 24) & 0xff;
  self->appStatusB3 = (ast >> 16) & 0xff;
  self->appStatusB3 = (ast >> 8) & 0xff;
  self->appStatusB3 = ast & 0xff;
  self->protocolStatus = protostatus;
  memset(self->reserved, 0, sizeof(self->reserved));
}


inline static void unknown_type_init(unknown_type_t *self, uint8_t unknown_type) {
  header_init((header_t *)self, TYPE_UNKNOWN, 0, sizeof(unknown_type_t)-sizeof(header_t));
  self->type = unknown_type;
  memset(self->reserved, 0, sizeof(self->reserved));
}

#endif
