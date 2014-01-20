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
import socket
from disc_utils import *
from disc_consts import *
import services
import hashlib
import socket

def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name
# end CamelCase


def str_to_class(class_name):
    return reduce(getattr, class_name.split("."), services)
# end str_to_class


def pub_sig(service, data):
    return hashlib.md5(service + json.dumps(data)).hexdigest()


class Subscribe(object):

    def __init__(self, dc, service_type, count, f=None, *args, **kw):
        self.dc = dc
        self.f = f
        self.kw = kw
        self.args = args
        self.count = count
        self.service_type = service_type
        self._headers = {
            'Content-type': 'application/json',
        }

        self.info = []
        infostr = json.dumps(self.info)
        self.sig = hashlib.md5(infostr).hexdigest()
        self.done = False

        data = {
            'service': service_type,
            'instances': count,
            'client-type': dc._client_type,
            'client': dc._myid
        }
        self.post_body = json.dumps(data)

        self.url = "http://%s:%s/subscribe" % (dc._server_ip, dc._server_port)

        if f:
            # asynch - callback when new info is received
            self.task = gevent.spawn(self.ttl_loop)
        else:
            self._query()
            self.done = True
    # end

    def ttl_loop(self):
        while True:
            self._query()

            # callback if service information has changed
            if self.change:
                self.f(self.info, *self.args, **self.kw)
                self.done = True

            # wait for next ttl expiry
            gevent.sleep(self.ttl)

    # info [{u'service_type': u'ifmap-server',
    #        u'ip_addr': u'10.84.7.1', u'port': u'8443'}]
    def _query(self):
        connected = False
        # hoping all errors are transient and a little wait will solve the problem
        while not connected:
            try:
                r = requests.post(
                    self.url, data=self.post_body, headers=self._headers)
                if r.status_code != 200:
                    # print 'Discovery Server returned error (code %d)' % (r.status_code)
                    gevent.sleep(2)
                else:
                    connected = True
            except requests.exceptions.ConnectionError:
                # discovery server down or restarting?
                gevent.sleep(2)
        # end while

        #print 'query resp => ', r.text
        response = r.json()

        # avoid signature on ttl which can change between iterations
        info = response[self.service_type]
        infostr = json.dumps(info)
        sig = hashlib.md5(infostr).hexdigest()

        # convert to strings
        for obj in info:
            if type(obj) is dict:
                for k, v in obj.items():
                    obj[k] = v.encode('utf-8')

        self.ttl = response['ttl']
        self.change = False
        if sig != self.sig:
            #print 'signature mismatch! old=%s, new=%s' % (self.sig, sig)
            self.info = info
            self.sig = sig
            self.change = True

    def wait(self):
        while not self.done:
            gevent.sleep(1)
    # end

    def read(self):
        return self.info


class DiscoveryClient(object):

    def __init__(self, server_ip, server_port, client_type):
        self._server_ip = server_ip
        self._server_port = server_port
        self._myid = socket.getfqdn() + ':' + client_type
        self._client_type = client_type
        self._headers = {
            'Content-type': 'application/json',
        }
        self.sig = None
        self.task = None

        # queue to publish information (sig => service data)
        self.pub_q = {}

        # token to re-publish information (token => service data)
        self.pubdata = {}

        # publish URL
        self.puburl = "http://%s:%s/publish" % (
            self._server_ip, self._server_port)

        # single task per publisher for heartbeats
        self.hbtask = gevent.spawn(self.heartbeat)

        # task to publish deferred information
        self.pub_task = None
    # end __init__

    def exportJson(self, o):
        obj_json = json.dumps(lambda obj: dict((k, v)
                              for k, v in obj.__dict__.iteritems()))
        return obj_json

    def _publish_int(self, service, data):
        #print 'Publish service "%s", data "%s"' % (service, data)
        payload = {service: data}
        try:
            r = requests.post(
                self.puburl, data=json.dumps(payload), headers=self._headers)
        except requests.exceptions.ConnectionError:
            # save request to retry later
            sig = pub_sig(service, data)
            if sig not in self.pub_q:
                #print 'ConnectionError: Saving %s for later' % (sig)
                self.pub_q[sig] = (service, data)

            # start publish task if needed
            if self.pub_task is None:
                #print 'Starting task to publish ...'
                self.pub_task = gevent.spawn(self.def_pub)
            return None

        # save cookie and published object in case we are forced to republish
        if r.status_code != 200:
            #print 'Discovery Server returned error (code %d)' % (r.status_code)
            #print 'Failed to publish service "%s" !!!' % (service)
            return None

        response = r.json()
        cookie = response['cookie']
        self.pubdata[cookie] = (service, data)
        #print 'Saving token %s' % (cookie)

        return cookie

    # publisher - send periodic heartbeat
    def heartbeat(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        delay = 1
        sock.settimeout(delay)
        while True:
            # send heartbeat for each published object seperately
            # dictionary can change size during iteration
            pub_list = self.pubdata.copy()
            for cookie in pub_list:
                data = '<cookie>%s</cookie>' % (cookie)
                sock.sendto(data, (self._server_ip, int(self._server_port)))
                try:
                    data = sock.recv(1024)
                except socket.timeout:
                    # discovery server down or restarting?
                    data = ''

                # if DS lost track of our data, republish
                if data == '404 Not Found':
                    # forget cached cookie and object; will re-learn
                    #print 'Server lost track of token %s' % (cookie)
                    service, data = self.pubdata[
                        cookie]
                    del self.pubdata[cookie]
                    self._publish_int(service, data)
            gevent.sleep(HC_INTERVAL)
    # end client

    # task to publish information which could not be published due to conn
    # errors
    def def_pub(self):
        while True:
            gevent.sleep(10)

            def_list = self.pub_q.copy()
            for sig, s_d in def_list.iteritems():
                service, data = s_d
                token = self._publish_int(service, data)
                if token:
                    #print 'Succeeded in publishing %s:%s' % (service, data)
                    del self.pub_q[sig]

            if not self.pub_q:
                #print 'Deferred list is empty. Done'
                self.pub_task = None
                return
    # end def_pub

    # API publish object
    # Tx {'name':u'ifmap-server', info:{u'ip_addr': u'10.84.7.1', u'port':
    # u'8443'}}
    def publish_obj(self, obj):
        service, data = obj.exportDict2()
        cookie = self._publish_int(service, data)

        # save token for later (unpublish etc.)
        if cookie:
            obj.token = cookie

        return self.hbtask
    # end publish

    # API publish service and data
    def publish(self, service, data):
        self._publish_int(service, data)
        return self.hbtask
    # end

    def _service_data_to_token(self, service, data):
        for token, s_d in self.pubdata.iteritems():
            s, d = s_d
            if service == s and data == d:
                return token
        return None

    def _un_publish(self, token):
        url = "http://%s:%s/service/%s" % (
            self._server_ip, self._server_port, token)
        requests.delete(url, headers=self._headers)

        # remove token and obj from records
        del self.pubdata[token]

    def un_publish_obj(self, obj):
        self._un_publish(obj.token)
    # end unpublish

    def un_publish(self, service, data):
        token = self._service_data_to_token(service, data)
        if token is None:
            #print 'Error: cannot find token for service %s, data %s'\
                #% (service, data)
            return
        self._un_publish(token)
    # end unpublish

    def get_task_id(self):
        return self.hbtask
    # end

    # subscribe request without callback is synchronous
    def subscribe(self, service_type, count, f=None, *args, **kw):
        obj = Subscribe(self, service_type, count, f, *args, **kw)
        return obj

    # retreive service information given service ID
    def get_service(self, sid):
        url = "http://%s:%s/service/%s" % (
            self._server_ip, self._server_port, sid)
        r = requests.get(url, headers=self._headers)
        #print 'get_service response = ', r

        entry = r.json()
        return entry

    # update service information (such as admin status)
    def update_service(self, sid, entry):
        url = "http://%s:%s/service/%s" % (
            self._server_ip, self._server_port, sid)
        body = json.dumps(entry)
        r = requests.put(url, data=body, headers=self._headers)
        #print 'update_service response = ', r
        return r.status_code
    # end get_service
