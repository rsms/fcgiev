#!/usr/bin/env python
# encoding: utf-8
import sys, os
sys.path[0:0] = [os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', 'lib')]
from eventlet import api, coros
from fcgiev import Server

def main():
  pool = coros.CoroutinePool(max_size=10)
  server = Server(api.tcp_listener(('127.0.0.1', 5000)), pool)
  server.run()

if __name__ == '__main__':
  #import cProfile;cProfile.run('main()', 'eventletfcgi.prof')
  main()
