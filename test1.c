#include "prefix.h"
#include "sockutil.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

#include <event.h>

#define FCGI_LISTENSOCK_FILENO 0
#define FLAG_KEEP_CONN 1
#define REQUESTS_MAX __SHRT_MAX__+__SHRT_MAX__

static void (*bufferevent_readcb)(int, short, void *) = NULL;
static void (*bufferevent_writecb)(int, short, void *) = NULL;


typedef struct {
  struct event ev;
} server_t;


typedef struct {
  uint16_t ident;
} request_t;


request_t *_requests[REQUESTS_MAX];


typedef enum {
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
} message_type_t;


typedef enum {
  REQUEST_COMPLETE = 0,
  CANT_MPX_CONN    = 1,
  OVERLOADED       = 2,
  UNKNOWN_ROLE     = 3
} protocol_status_t;


typedef struct {
  uint8_t version;
  uint8_t type;
  uint8_t requestIdB1;
  uint8_t requestIdB0;
  uint8_t contentLengthB1;
  uint8_t contentLengthB0;
  uint8_t paddingLength;
  uint8_t reserved;
} header_t;


typedef struct {
  uint8_t roleB1;
  uint8_t roleB0;
  uint8_t flags;
  uint8_t reserved[5];
} begin_request_t;


typedef struct {
  header_t header;
  uint8_t appStatusB3;
  uint8_t appStatusB2;
  uint8_t appStatusB1;
  uint8_t appStatusB0;
  uint8_t protocolStatus;
  uint8_t reserved[3];
} end_request_t;


typedef struct {
  header_t header;
  uint8_t type;
  uint8_t reserved[7];
} unknown_type_t;


void header_init(header_t *self, message_type_t t, uint16_t id, uint16_t len) {
  self->version = t;
  self->requestIdB1 = id >> 8;
  self->requestIdB0 = id & 0xff;
  self->contentLengthB1 = len >> 8;
  self->contentLengthB0 = len & 0xff;
  self->paddingLength = '\0';
  self->reserved = '\0';
}


void end_request_init(end_request_t *self, uint16_t id, uint32_t ast, protocol_status_t pst) {
  header_init((header_t *)self, TYPE_END_REQUEST, id, sizeof(end_request_t)-sizeof(header_t));
  self->appStatusB3 = (ast >> 24) & 0xff;
  self->appStatusB3 = (ast >> 16) & 0xff;
  self->appStatusB3 = (ast >> 8) & 0xff;
  self->appStatusB3 = ast & 0xff;
  self->protocolStatus = pst;
  memset(self->reserved, 0, sizeof(self->reserved));
}


void unknown_type_init(unknown_type_t *self, uint8_t unknown_type) {
  header_init((header_t *)self, TYPE_UNKNOWN, 0, sizeof(unknown_type_t)-sizeof(header_t));
  self->type = unknown_type;
  memset(self->reserved, 0, sizeof(self->reserved));
}


#define BEV_FD(p) ((p)->bev.ev_read.ev_fd)
#define BEV_FD_SET(p, v) ((p)->bev.ev_read.ev_fd = (v))


// get a request by request id
request_t *request_get(uint16_t id) {
  request_t *r;
  r = _requests[id];
  if (r == NULL) {
    r = (request_t *)calloc(1, sizeof(request_t));
    _requests[id] = r;
  }
  return r;
}

// restore a request object
void request_put(request_t *r) {
  // eeeh... nothing.
}


void bev_close(struct bufferevent *bev) {
  bufferevent_disable(bev);
	close(BEV_FD(bev));
  BEV_FD_SET(bev, -1);
}


void bev_abort(struct bufferevent *bev) {
  struct linger lingeropt;
  lingeropt.l_onoff = 1;
  lingeropt.l_linger = 0;
  AZ(setsockopt(BEV_FD(bev), SOL_SOCKET, SO_LINGER, (char *)&lingeropt, sizeof(lingeropt)));
  shutdown(BEV_FD(bev), SHUT_RDWR);
  evbuffer_drain(bev->input, EVBUFFER_LENGTH(bev->input));
  evbuffer_drain(bev->output, EVBUFFER_LENGTH(bev->output));
  bev_close(bev);
}


void fcgiproto_errorcb(struct bufferevent *bev, short what, request_t *r) {
  if (what & EVBUFFER_EOF)
    printf("request %p EOF\n", r);
  else if (what & EVBUFFER_TIMEOUT)
    printf("request %p timeout\n", r);
  else
    printf("request %p error\n", r);
  
  bev_close(bev);
  if (r)
    request_put(r);
}


void fcgiproto_readcb(struct bufferevent *bev, request_t *r) {
  printf("fcgiproto_readcb(%p, %p)\n", bev, r);
  //bufferevent_write_buffer(bev, bev->input);
  
  while(EVBUFFER_LENGTH(bev->input) >= sizeof(header_t)) {
    const header_t *hp = (const header_t *)EVBUFFER_DATA(bev->input);
    
    // Check whether our peer speaks the correct protocol version.
    if (hp->version != 1) {
      warnx("fcgiev: cannot handle protocol version %u", hp->version);
      request_abort(r);
      evbuffer_drain(bev->input, EVBUFFER_LENGTH(bev->input));
      break;
    }
    
    // Check whether we have the whole message that follows the
    // headers in our buffer already. If not, we can't process it
    // yet.
    uint16_t msg_len = (hp->contentLengthB1 << 8) + hp->contentLengthB0;
    uint16_t msg_id  = (hp->requestIdB1 << 8) + hp->requestIdB0;
    
    if (EVBUFFER_LENGTH(bev->input) < sizeof(header_t) + msg_len + hp->paddingLength)
      return;
    
    // Process the message.
    // todo
  }
}


void fcgiproto_writecb(struct bufferevent *bev, request_t *r) {
  printf("fcgiproto_writecb(%p, %p)\n", bev, r);
}


/*void conn_init(request_t *r, int fd) {
  r->ident = 0;
	r->bev.readcb = (evbuffercb)fcgiproto_readcb;
  r->bev.writecb = (evbuffercb)fcgiproto_writecb;
	r->bev.errorcb = (everrorcb)fcgiproto_errorcb;
	r->bev.cbarg = (void *)r;
  event_set(&r->bev.ev_read, fd, EV_READ, bufferevent_readcb, (void *)&r->bev);
	event_set(&r->bev.ev_write, fd, EV_WRITE, bufferevent_writecb, (void *)&r->bev);
  r->bev.enabled = EV_WRITE;
}*/


void server_init(server_t *server) {
  server->ev.ev_fd = FCGI_LISTENSOCK_FILENO;
}


bool server_bind(server_t *server, const char *addrorpath) {
  if ((server->ev.ev_fd = sockutil_bind(addrorpath, SOMAXCONN)) < 0)
    return false;
  return true;
}


void server_accept(int fd, short ev, server_t *server) {
  struct bufferevent *bev;
  socklen_t saz;
  int on = 1, events, connfd;
  struct timeval *timeout;
  sau_t sa;
  
  saz = sizeof(sa);
  connfd = accept(fd, (struct sockaddr *)&sa, &saz);
  timeout = NULL;
  
  if (connfd < 0) {
    warn("accept failed");
    return;
  }
  
  // Disable Nagle -- better response times at the cost of more packets being sent.
  setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, (char *)&on, sizeof(on));
  // Set nonblocking
  AZ(ioctl(connfd, FIONBIO, (int *)&on));
  
  bev = bufferevent_new(connfd, (evbuffercb)fcgiproto_readcb,
    (evbuffercb)fcgiproto_writecb, (everrorcb)fcgiproto_errorcb, NULL);
  
  events = EV_READ;
  if (bev->writecb)
    events |= EV_WRITE;
  bufferevent_enable(bev, events);
}


void server_run(server_t *server) {
  int on = 1;
  AZ(ioctl(server->ev.ev_fd, FIONBIO, (int *)&on));
  event_set(&server->ev, server->ev.ev_fd, EV_READ|EV_PERSIST,
    (void (*)(int,short,void*))server_accept, (void *)server);
  event_add(&server->ev, NULL/* no timeout */);
  event_dispatch();
}


void fcgiev_init() {
  struct bufferevent *bev;
  if (bufferevent_readcb != NULL)
    return;
  bev = bufferevent_new(0, (evbuffercb)NULL, (evbuffercb)NULL, (everrorcb)NULL, NULL);
  bufferevent_readcb = bev->ev_read.ev_callback;
  bufferevent_writecb = bev->ev_write.ev_callback;
  bufferevent_free(bev);
  memset((void *)_requests, 0, sizeof(_requests));
}


int main(void) {
  server_t server;
  
  event_init();
  fcgiev_init();
  
  server_init(&server);
  server_bind(&server, "127.0.0.1:5000");
  server_run(&server);
  
  return 0;
}