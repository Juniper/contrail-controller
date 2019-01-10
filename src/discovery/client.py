#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
# note that any print to stdout will break node manager because its uses
# stdout as communication channel and gratitious text there will break it.
# stderr should be fine

import os
import sys
import gevent
from gevent import monkey
if not 'unittest' in sys.modules:
    monkey.patch_all()

import requests
import uuid
from cfgm_common import jsonutils as json
from cfgm_common import rest, utils
import hashlib
import socket
from disc_utils import *
from disc_consts import *
import services
import hashlib
import socket

# sandesh
from sandesh.discovery_client import ttypes as sandesh
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType

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

class DiscoveryRequest(object):
    def _request(self, op, url, data=None, headers=None, timeout=30):
        if (op == rest.OP_GET):
            if self.verify:
                resp = requests.get(
                        url, headers=self._headers, verify=self.verify)
            else:
                resp = requests.get(url, headers=self._headers)
        elif (op == rest.OP_POST):
            if self.verify:
                resp = requests.post(
                        url, data=data, headers=self._headers, timeout=timeout,
                        verify=self.verify)
            else:
                resp = requests.post(
                        url, data=data, headers=self._headers, timeout=timeout)
        elif (op == rest.OP_DELETE):
            if self.verify:
                resp = requests.delete(
                        url, headers=self._headers, verify=self.verify)
            else:
                resp = requests.delete(url, headers=self._headers)
        elif (op == rest.OP_PUT):
            if self.verify:
                resp = requests.put(
                        url, data=data, headers=self._headers, verify=self.verify)
            else:
                resp = requests.put(url, data=data, headers=self._headers)

        return resp


class Subscribe(DiscoveryRequest):

    def __init__(self, dc, service_type, count, f=None, *args, **kw):
        super(Subscribe, self).__init__()
        self.verify = kw.pop("verify")
        self.protocol = "http"
        if self.verify:
            self.protocol = "https"
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

        self.stats = {
            'service_type' : service_type,
            'request'      : 0,
            'response'     : 0,
            'conn_error'   : 0,
            'timeout'      : 0,
            'exc_unknown'  : 0,
            'exc_info'     : '',
            'instances'    : count,
            'ttl'          : 0,
            'blob'         : '',
        }

        self.data = {
            'service': service_type,
            'instances': count,
            'client-type': dc._client_type,
            'remote-addr': dc.remote_addr,
            'client': dc._myid
        }
        self.post_body = json.dumps(self.data)

        self.url = "%s://%s:%s/subscribe" % (self.protocol, dc._server_ip, dc._server_port)

        if f:
            # asynch - callback when new info is received
            ConnectionState.update(conn_type = ConnectionType.DISCOVERY,
                name = self.service_type,
                status = ConnectionStatus.INIT,
                message = 'Subscribe',
                server_addrs = ['%s:%s' % (dc._server_ip, dc._server_port)])
            self.task = gevent.spawn(self.ttl_loop)
        else:
            self._query()
            self.done = True
    # end

    def update_subscribe_data(self, key, val):
        self.data[key] = val
        self.post_body = json.dumps(self.data)

    def inc_stats(self, key):
        if key not in self.stats:
            self.stats[key] = 0
        self.stats[key] += 1

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
        conn_state_updated = False
        # hoping all errors are transient and a little wait will solve the problem
        while True:
            try:
                self.stats['request'] += 1
                r = self._request(
                        rest.OP_POST, self.url, data=self.post_body,
                        headers=self._headers, timeout=30)
                if r.status_code == 200:
                    break
                self.inc_stats('sc_%d' % r.status_code)
                emsg = "Status Code %d" % r.status_code
            except requests.exceptions.ConnectionError:
                self.stats['conn_error'] += 1
                emsg = 'Connection Error'
            except (requests.exceptions.Timeout, socket.timeout):
                self.stats['timeout'] += 1
                emsg = 'Request Timeout'
            except Exception as e:
                self.stats['exc_unknown'] += 1
                emsg = str(e)
            self.syslog('connection error or failed to subscribe')
            if not conn_state_updated:
                conn_state_updated = True
                ConnectionState.update(
                    conn_type = ConnectionType.DISCOVERY,
                    name = self.service_type,
                    status = ConnectionStatus.DOWN,
                    message = 'Subscribe - %s' % emsg,
                    server_addrs = \
                        ['%s:%s' % (self.dc._server_ip, \
                         self.dc._server_port)])
            gevent.sleep(2)
        # end while

        self.syslog('query resp => %s ' % r.text)
        response = r.json()

        # avoid signature on ttl which can change between iterations
        info = response[self.service_type]
        infostr = json.dumps(info)
        sig = hashlib.md5(infostr).hexdigest()

        self.stats['response'] += 1
        self.stats['ttl'] = response['ttl']
        self.stats['blob'] = infostr

        self.ttl = response['ttl']
        self.change = False
        if sig != self.sig:
            #print 'signature mismatch! old=%s, new=%s' % (self.sig, sig)
            self.info = info
            self.sig = sig
            self.change = True

        ConnectionState.update(conn_type = ConnectionType.DISCOVERY,
            name = self.service_type,
            status = ConnectionStatus.UP,
            message = 'Subscribe Response',
            server_addrs = ['%s:%s' % (self.dc._server_ip, \
                self.dc._server_port)])

    def wait(self):
        while not self.done:
            gevent.sleep(1)
    # end

    def read(self):
        return self.info

    def syslog(self, log_msg):
	self.dc.syslog(log_msg)

class DiscoveryClient(DiscoveryRequest):
    _DEFAULT_CERT_BUNDLE="discoverycertbundle.pem"

    def __init__(self, server_ip, server_port, client_type, pub_id=None,
                 **kwargs):
        super(DiscoveryClient, self).__init__()
        self._server_ip = server_ip
        self._server_port = server_port
        self._pub_id = pub_id or socket.gethostname()
        self._myid = self._pub_id + ':' + client_type
        self._client_type = client_type
        self._headers = {
            'Content-type': 'application/json',
        }
        self.sig = None
        self.task = None
        self._sandesh = None

        self._verify = False
        self._cert = kwargs.get('cert', None)
        self._key = kwargs.get('key', None)
        self._cacert = kwargs.get('cacert', None)

        self.stats = {
            'client_type'    : client_type,
            'hb_iters'       : 0,
        }
        self.pub_stats = {}
        self._myip = '127.0.0.1'

        # queue to publish information (sig => service data)
        self.pub_q = {}

        # token to re-publish information (token => service data)
        self.pubdata = {}
        self.pub_data = {}

        # publish URL
        self.protocol = "http"
        if self.verify:
            self.protocol = "https"
        self.puburl = "%s://%s:%s/publish/%s" % (
            self.protocol, self._server_ip, self._server_port, self._pub_id)
        self.hburl = "%s://%s:%s/heartbeat" % (
            self.protocol, self._server_ip, self._server_port)

        # single task per publisher for heartbeats
        self.hbtask = gevent.spawn(self.heartbeat)

        # task to publish deferred information
        self.pub_task = None
        self._subs = []
    # end __init__

    @property
    def verify(self):
        # bundle created already return bundle
        if self._verify:
            return self._verify
        if self._cacert:
            certs=[self._cacert]
            if self._key and self._cert:
                certs=[self._cert, self._key, self._cacert]
                certbundle = os.path.join(
                    '/tmp', self._server_ip.replace('.', '_'),
                     DiscoveryClient._DEFAULT_CERT_BUNDLE)
                self._verify = utils.getCertKeyCaBundle(certbundle, certs)

        return self._verify

    @property
    def remote_addr(self):
        if self._myip != '127.0.0.1':
            return self._myip
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect((self._server_ip, self._server_port))
            self._myip = s.getsockname()[0]
            s.close()
        except Exception as e:
            pass
        return self._myip

    @remote_addr.setter
    def remote_addr(self, remote_addr):
        self._myip = remote_addr

    def set_sandesh(self, sandesh):
        self._sandesh = sandesh
    # end set_sandesh

    def syslog(self, log_msg):
        if self._sandesh is None:
            return
        log = sandesh.discClientLog(
            log_msg=log_msg, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

    def inc_stats(self, key):
        if key not in self.stats:
            self.stats[key] = 0
        self.stats[key] += 1

    def inc_pub_stats(self, service, key, value = None):
        if service not in self.pub_stats:
            self.pub_stats[service] = {}
            self.pub_stats[service]['service-type'] = service
            self.pub_stats[service]['request'] = 0
            self.pub_stats[service]['response'] = 0
            self.pub_stats[service]['conn_error'] = 0
            self.pub_stats[service]['timeout'] = 0
            self.pub_stats[service]['exc_unknown'] = 0
            self.pub_stats[service]['exc_info'] = ''
            self.pub_stats[service]['blob'] = ''
        if key not in self.pub_stats[service]:
            self.pub_stats[service][key] = 0
        if value:
            self.pub_stats[service][key] = value
        else:
            self.pub_stats[service][key] += 1

    def get_stats(self):
        stats = self.stats.copy()
        stats['subs'] = [sub.stats for sub in self._subs]
        stats['pubs'] = self.pub_stats
        return stats

    def exportJson(self, o):
        obj_json = json.dumps(lambda obj: dict((k, v)
                              for k, v in obj.__dict__.iteritems()))
        return obj_json

    def _publish_int(self, service, data, oper_state = 'up', msg = ''):
        self.syslog('Publish service "%s", data "%s"' % (service, data))
        payload = {
            service             : data,
            'service-type'      : service,
            'remote-addr'       : self.remote_addr,
            'oper-state'        : oper_state,
            'oper-state-reason' : msg
        }
        emsg = None
        cookie = None
        try:
            self.inc_pub_stats(service, 'request')
            r = self._request(
                    rest.OP_POST, self.puburl, data=json.dumps(payload),
                    headers=self._headers, timeout=30)
            if r.status_code != 200:
                self.inc_pub_stats(service, 'sc_%d' % r.status_code)
                emsg = 'Status Code ' + str(r.status_code)
        except requests.exceptions.ConnectionError:
            self.inc_pub_stats(service, 'conn_error')
            emsg = 'Connection Error'
        except requests.exceptions.Timeout:
            self.inc_pub_stats(service, 'timeout')
            emsg = 'Request Timeout'
        finally:
            ConnectionState.update(conn_type = ConnectionType.DISCOVERY,
                name = service,
                status = ConnectionStatus.DOWN if emsg else ConnectionStatus.UP,
                server_addrs = ['%s:%s' % (self._server_ip, \
                    self._server_port)],
                message = 'Publish Error - %s' % emsg if emsg else 'Publish Success')

        if not emsg:
            self.inc_pub_stats(service, 'response')
            self.inc_pub_stats(service, 'blob', value = json.dumps(data))
            response = r.json()
            cookie = response['cookie']
            self.pubdata[cookie] = (service, data)
            self.syslog('Saving token %s' % (cookie))
        return cookie

    # publisher - send periodic heartbeat
    def heartbeat(self):
        while True:
            self.stats['hb_iters'] += 1
            # Republish each published object seperately
            # dictionary can change size during iteration
            for service, data in self.pub_data.items():
                gevent.sleep(0)
                try:
                    self._publish_int(service, data)
                except Exception as e:
                    self.inc_pub_stats(service, 'exc_unknown')
                    msg = 'Error in publish service %s - %s' %(service, str(e))
                    self.syslog(msg)
            gevent.sleep(HC_INTERVAL)
    # end client

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
    def publish(self, service, data, state = 'up', msg = ''):
        self.pub_data[service] = data
        self._publish_int(service, data, state, msg)
        return self.hbtask
    # end

    def _service_data_to_token(self, service, data):
        for token, s_d in self.pubdata.iteritems():
            s, d = s_d
            if service == s and data == d:
                return token
        return None

    def _un_publish(self, token):
        url = "%s://%s:%s/service/%s" % (
            self.protocol, self._server_ip, self._server_port, token)
        self._request(rest.OP_DELETE, url, headers=self._headers)

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
        kw.update({"verify" : self.verify})
        obj = Subscribe(self, service_type, count, f, *args, **kw)
        if f:
            self._subs.append(obj)
        return obj

    # retreive service information given service ID
    def get_service(self, sid):
        url = "%s://%s:%s/service/%s" % (
            self.protocol, self._server_ip, self._server_port, sid)
        r = self._request(rest.OP_GET, url, headers=self._headers)
        #print 'get_service response = ', r

        entry = r.json()
        return entry

    # update service information (such as admin status)
    def update_service(self, sid, entry):
        url = "%s://%s:%s/service/%s" % (
            self.protocol, self._server_ip, self._server_port, sid)
        body = json.dumps(entry)
        r = self._request(rest.OP_PUT, url, data=body, headers=self._headers)
        #print 'update_service response = ', r
        return r.status_code
    # end get_service

    def uninit(self):
        glets = map(lambda x: x.task, self._subs) + [self.hbtask]
        gevent.killall(glets)
        gevent.joinall(glets)
