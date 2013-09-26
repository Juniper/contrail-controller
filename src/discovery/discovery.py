#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()
import gevent
import requests
import uuid
import json
import hashlib
import sys
from services import *
from disc_health import HealthCheck


def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name


def str_to_class(class_name):
    return reduce(getattr, class_name.split("."), sys.modules[__name__])
# end str_to_class

# end CamelCase


class DiscoveryService(object):

    def __init__(self, server_ip, server_port, id=None):
        self._server_ip = server_ip
        self._server_port = server_port
        self._myid = id or uuid.uuid4()
        self._headers = {
            'Content-type': 'application/json',
        }
        self.sig = None
        self.task = None
    # end __init__

    def exportJson(self, o):
        obj_json = json.dumps({k: v for k, v in o.__dict__.iteritems()})
        return obj_json

    # publish service
    # Tx {'name':u'ifmap-server', info:{u'ip_addr': u'10.84.7.1', u'port':
    # u'8443'}}
    def publish(self, obj):
        url = "http://%s:%s/publish" % (self._server_ip, self._server_port)
        body = json.dumps(obj.exportDict())
        response = requests.post(url, data=body, headers=self._headers)
        print 'Register response = ', response

        # send published information as heartbeat data
        hc = HealthCheck(self._server_ip, int(self._server_port))
        hc.set_heartbeat_data(response.text)
        return gevent.spawn(hc.client)
    # end publish

    # info [{u'service_type': u'ifmap-server',
    #        u'ip_addr': u'10.84.7.1', u'port': u'8443'}]
    def _query(self):
        r = requests.post(
            self.url, data=self.post_body, headers=self._headers)
        print 'query resp => ', r.text

        # avoid signature on ttl which can change between iterations
        info = r.json[self.service_type]
        infostr = json.dumps(info)
        sig = hashlib.md5(infostr).hexdigest()

        # convert to strings
        for obj_dict in info:
            for k, v in obj_dict.items():
                obj_dict[k] = v.encode('utf-8')

        self.ttl = r.json['ttl']
        self.change = False
        if sig != self.sig:
            print 'signature mismatch! old=%s, new=%s' % (self.sig, sig)
            self.info = info
            self.sig = sig
            self.change = True

    # loop to periodically re-subscribe after TTL expiry
    def ttl_loop(self):
        while True:
            self._query()

            # callback if service information has changed
            if self.change:
                objs = []
                class_name = CamelCase(self.service_type)
                cls = str_to_class(class_name)
                for obj_dict in self.info:
                    obj = cls.factory(**obj_dict)
                    objs.append(obj)
                self.f(objs, *self.args, **self.kw)

            # wait for next ttl expiry
            gevent.sleep(self.ttl)

    # subscribe request without callback is synchronous
    def subscribe(self, service_type, count, f=None, *args, **kw):
        self.f = f
        self.kw = kw
        self.args = args
        self.count = count
        self.service_type = service_type

        self.info = []
        infostr = json.dumps(self.info)
        self.sig = hashlib.md5(infostr).hexdigest()

        self.post_body = json.dumps(
            {'service': service_type, 'instances': count,
             'client': self._myid})
        self.url = "http://%s:%s/subscribe" % (
            self._server_ip, self._server_port)

        if f:
            # asynch - callback when new info is received
            self.task = gevent.spawn(self.ttl_loop)
            return self.task

        self._query()
        return self.read()

    def read(self):
        r = []
        class_name = CamelCase(self.service_type)
        cls = str_to_class(class_name)
        for obj_dict in self.info:
            obj = cls.factory(**obj_dict)
            r.append(obj)
        return r
    # end read
