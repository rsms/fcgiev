#!/usr/bin/env python
# encoding: utf-8
import sys, os
sys.path[0:0] = [os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', 'lib')]
from eventlet import api, coros
from fcgiev import Server

def fcgi_stdin_sock():
  import socket
  from eventlet.wrappedfd import wrapped_fd
  return wrapped_fd(socket.fromfd(0, socket.AF_UNIX, socket.SOCK_STREAM))

def accept(sock, handler):
  try:
    print 'accepting connections on', sock.getsockname()
    while True:
      try:
        handler(*sock.accept())
      except KeyboardInterrupt:
        api.get_hub().remove_descriptor(sock.fileno())
        break
  finally:
    try:
      sock.close()
    except socket.error:
      pass


def run(spawner, sockets, acceptor, handler):
  waiters = []
  for sock in sockets:
    waiters.append(spawner(acceptor, sock, handler))
  
  # wait for all the coroutines to come back before exiting the process
  for waiter in waiters:
    waiter.wait()


def main():
  from optparse import OptionParser
  parser = OptionParser(usage="usage: %prog [options]")
  parser.add_option("-D", "--debug",
                    dest="debug",
                    help="debug mode.",
                    action="store_true",
                    default=False)
  parser.add_option("-c", "--cpool",
                    dest="cpool_size",
                    help="size of coroutine pool.",
                    metavar="<size>",
                    type="int",
                    default=10)
  opts, args = parser.parse_args()
  
  pool = None
  spawner = api.spawn
  sockets = []
  
  if opts.cpool_size > 0:
    pool = coros.CoroutinePool(max_size=opts.cpool_size)
    spawner = pool.execute
  
  if len(args) == 0:
    sockets.append(fcgi_stdin_sock())
  else:
    for arg in args:
      # todo: add support for UNIX sockets
      addr = arg.replace('[','').replace(']','').rsplit(':', 1)
      if len(addr) != 2:
        parser.error('arguments must be IPv4 or IPv6 addresses including port')
      addr = (addr[0], int(addr[1]))
      sockets.append(api.tcp_listener(addr))
  
  server = Server(None, pool)
  run(spawner, sockets, accept, server.handle_connection)
  #server.run()

if __name__ == '__main__':
  #import cProfile;cProfile.run('main()', 'eventletfcgi.prof')
  main()
