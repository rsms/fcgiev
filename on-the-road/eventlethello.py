#!/usr/bin/env python
# encoding: utf-8
'''A very simple HTTP server using eventlet (which in turn is based on greenlet)
'''
from eventlet import api, httpd, util
 
util.wrap_socket_with_coroutine_socket()
 
class Handler(object):
  def handle_request(self, req):
    path = req.path_segments()
    if len(path) > 0 and path[0] == "notexist":
      req.response(404, body='not found')
      return
    api.sleep(0.2)
    req.write('hello world\n')
  
  def adapt(self, obj, req):
    'Convert obj to bytes'
    req.write(str(obj))
  
 
def main():
  class deadlog(object):
    def write(self, s, l=0):
      pass
  
  httpd.server(api.tcp_listener(('127.0.0.1', 8090)), Handler(), max_size=5000, log=deadlog())
 
if __name__ == '__main__':
  #import cProfile;cProfile.run('main()', 'eventlethello.prof')
  main()
