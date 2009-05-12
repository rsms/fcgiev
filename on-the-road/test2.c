#include <stdio.h>
#include <assert.h>

#include <event.h>
#include <evhttp.h>

#ifndef NDEBUG
  #define AZ(foo) assert((foo) == 0)
  #define AN(foo) assert((foo) != 0)
  #define log_debug(fmt, ...) \
    fprintf(stderr, "D %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
  #define AZ(foo) (foo)
  #define AN(foo) (foo)
  #define log_debug(fmt, ...) (0)
#endif


void request_handler(struct evhttp_request *request, void *_server) {
  struct evbuffer *stdout = NULL;
  stdout = evbuffer_new();
  evbuffer_add_printf(stdout, "Hello world!\n");
  evhttp_send_reply(request, HTTP_OK, "OK", stdout);
}


int main(int argc, const char * const *argv) {
  struct evhttp *server = NULL;
  int i;
  
  event_init();
  AN(server = evhttp_start("127.0.0.1", 8080));
  evhttp_set_gencb(server, request_handler, (void *)server);
  event_dispatch();
  
  return 0;
}
// gcc -o test2 -O3 -finline-functions -ffast-math -funroll-all-loops -msse3 -L/opt/local/lib -I/opt/local/include -levent test2.c
// 