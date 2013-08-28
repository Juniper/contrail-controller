#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import socket
import sys
from disc_utils import *
from disc_consts import *
import xmltodict

class HealthCheck(object):

    def __init__(self, host, port):
        self.host = host
        self.port = port
    #end __init__

    def set_server_callback(self, f=None, *args, **kw):
        self.f = f
        self.args = args
        self.kw = kw
    #end hb_callback

    # discovery server - process heartbeat from publishers
    def server(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((self.host, self.port))
        while True:
            data, addr = sock.recvfrom(1024)
            """
            print ''
            print 'addr = ', addr
            print 'data = ', data
            """
            data = xmltodict.parse(data)
            self.f(data['cookie'], *self.args, **self.kw)
    #end start

    # set data to send in heartbeat
    def set_heartbeat_data(self, data):
        self.hbdata = '<cookie>%s</cookie>' %(data)
    #end set_id

    # publisher - send periodic heartbeat
    def client(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        while True:
            data = self.hbdata
            sock.sendto(data, (self.host, self.port))
            gevent.sleep(HC_INTERVAL)
    #end client

#end class HealthCheck
