#ifndef _FCGIEV_BUFFER_H_
#define _FCGIEV_BUFFER_H_

#include <stdint.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
  u_char *buffer;
  u_char *orig_buffer;
  size_t misalign;
  size_t totallen;
  size_t off;
} buf_t;


#define BUF_LENGTH(x) (x)->off
#define BUF_DATA(x) (x)->buffer
#define BUF_INPUT(x) (x)->input
#define BUF_OUTPUT(x) (x)->output


inline static buf_t *buf_new(void) {
  buf_t *buffer;
  buffer = calloc(1, sizeof(buf_t));
  return buffer;
}


inline static void buf_free(buf_t *b) {
  if (b->orig_buffer != NULL)
    free(b->orig_buffer);
  free(b);
}


inline static void buf_align(buf_t *buf) {
  memmove(buf->orig_buffer, buf->buffer, buf->off);
  buf->buffer = buf->orig_buffer;
  buf->misalign = 0;
}


static void buf_reserve(buf_t *buf, size_t size) {
  size_t need = buf->misalign + buf->off + size;
  
  /* If we can fit all the data, then we don't have to do anything */
  if (buf->totallen >= need)
    return;
  
  /*
   * If the misalignment fulfills our data needs, we just force an
   * alignment to happen.  Afterwards, we have enough space.
   */
  if (buf->misalign >= size) {
    buf_align(buf);
  } 
  else {
    void *newbuf;
    size_t length = buf->totallen;
    
    if (length < 256)
      length = 256;
    
    while (length < need)
      length <<= 1;
    
    if (buf->orig_buffer != buf->buffer)
      buf_align(buf);
    
    AN(newbuf = realloc(buf->buffer, length));
    
    buf->orig_buffer = buf->buffer = newbuf;
    buf->totallen = length;
  }
}


inline static void buf_append(buf_t *buf, const void *data, size_t len) {
  if (buf->totallen < buf->misalign + buf->off + len)
    buf_reserve(buf, len);
  memcpy(buf->buffer + buf->off, data, len);
  buf->off += len;
}


static void buf_drain(buf_t *buf, size_t len) {
  if (len >= buf->off) {
    buf->off = 0;
    buf->buffer = buf->orig_buffer;
    buf->misalign = 0;
    return;
  }
  
  buf->buffer += len;
  buf->misalign += len;
  buf->off -= len;
}

#endif
