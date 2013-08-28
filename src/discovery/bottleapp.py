#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
from gevent import monkey; monkey.patch_all()
import bottle
from discovery import *
from services import *
import sys

@bottle.route('/')
def hello():
    return "Hello World!"

def expiry():
	print '!!! Expired !!!!'

def handle_results(info):
	print '!!! In Callback handler !!!'
        print 'Info = ', info

def ifmap_server_handler(obj):
	print '!!! In Callback handler !!!'
        print 'Service info = %s/%s' %(obj.get_ip_addr(), obj.get_port())

discover = DiscoveryService('127.0.0.1', '5998')
print 'Connected with discovery service'
#result = discover.IfmapServer(f=handle_results)
#print 'Subscribe result = ', result

discover.subscribe('ifmap-server', 1, f=ifmap_server_handler)
#print 'Subscribe result = ', result

#timer = disc_utils.LoopingCall(expiry)
#timer.start(2)
#gevent.spawn(expiry)
#gevent.sleep(1)

app = bottle.app()
bottle.run(app = app, host='10.84.7.1', port='5997', server = 'gevent')
#bottle.run(app = app, host='10.84.7.1', port='5997')
