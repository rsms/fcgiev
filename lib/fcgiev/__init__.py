# encoding: utf-8
'''fcgiev
'''
from fcgiev.release import __version__
import _fcgiev
import socket
from eventlet import api, util

#util.wrap_socket_with_coroutine_socket()

class Request(object):
  def __init__(self):
    self.id = 0
  

class Server(object):
  def __init__(self, sock, coro_pool=None, max_requests=32000):
    self.sock = sock
    self.requests = []
    if coro_pool is not None:
      self.spawner = coro_pool.execute_async
    else:
      self.spawner = api.spawn
  
  def handle_connection(self, sock, addr):
    #coro = api.getcurrent()
    import sys
    print >> sys.stderr, "client %s connected" % repr(addr)
    try:
      _fcgiev.process(sock.fd, self.spawner, api.trampoline)
    except socket.error, e:
      # 54: Connection reset by peer
      # 32: Broken pipe (connection closed)
      if e.args[0] not in (54, 32):
        raise
    #print "client %s:%d disconnected" % addr
  
  def run(self):
    try:
      while True:
        try:
          self.handle_connection(*self.sock.accept())
        except KeyboardInterrupt:
          api.get_hub().remove_descriptor(self.sock.fileno())
          break
    finally:
      try:
        self.sock.close()
      except socket.error:
        pass
  
