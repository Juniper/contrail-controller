#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This is the main module in discovery service package. It manages interaction
between http/rest and database interfaces.
"""

import gevent
from gevent import monkey
monkey.patch_all()
from gevent import hub
from disc_cassdb import DiscoveryCassandraClient

import sys
import time
import logging
import signal
import os
import socket
from cfgm_common import jsonutils as json
import xmltodict
import uuid
import copy
import datetime
import argparse
import ConfigParser
from pprint import pformat
import random
from netaddr import IPNetwork, IPAddress, IPSet, IPRange

import bottle

from disc_chash import *
from disc_utils import *
import disc_consts
import disc_exceptions
import output
import discoveryclient.client as discovery_client

# sandesh
from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.ttypes import Module, NodeType
from pysandesh.connection_info import ConnectionState
from sandesh.discovery_introspect import ttypes as sandesh
from sandesh.nodeinfo.ttypes import NodeStatusUVE, NodeStatus
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames,\
    INSTANCE_ID_DEFAULT

from gevent.coros import BoundedSemaphore
from cfgm_common.rest import LinkObject

import disc_auth_keystone

def obj_to_json(obj):
    # Non-null fields in object get converted to json fields
    return lambda obj: dict((k, v) for k, v in obj.__dict__.iteritems())
# end obj_to_json

class DiscoveryServer():

    def __init__(self, args):
        self._homepage_links = []
        self._args = args
        self.service_config = args.service_config
        self.cassandra_config = args.cassandra_config
        self._debug = {
            'hb_stray': 0,
            'msg_pubs': 0,
            'msg_subs': 0,
            'msg_query': 0,
            'msg_hbt': 0,
            'ttl_short': 0,
            'policy_rr': 0,
            'policy_lb': 0,
            'policy_fi': 0,
            'policy_chash': 0,
            'db_upd_hb': 0,
            'throttle_subs':0,
            '503': 0,
            'lb_count': 0,
            'lb_auto': 0,
            'lb_partial': 0,
            'lb_full': 0,
            'db_exc_unknown': 0,
            'db_exc_info': '',
            'wl_rejects_pub': 0,
            'wl_rejects_sub': 0,
            'auth_failures': 0,
        }
        self._ts_use = 1
        self.short_ttl_map = {}
        self._sem = BoundedSemaphore(1)
        self._pub_wl = None
        self._sub_wl = None

        self._base_url = "http://%s:%s" % (self._args.listen_ip_addr,
                                           self._args.listen_port)
        self._pipe_start_app = None

        bottle.route('/', 'GET', self.homepage_http_get)

        # heartbeat
        bottle.route('/heartbeat', 'POST', self.api_heartbeat)

        # publish service
        bottle.route('/publish', 'POST', self.api_publish)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/publish', 'publish service'))
        bottle.route('/publish/<end_point>', 'POST', self.api_publish)

        # subscribe service
        bottle.route('/subscribe',  'POST', self.api_subscribe)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/subscribe', 'subscribe service'))

        # query service
        bottle.route('/query',  'POST', self.api_query)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/query', 'query service'))

        # collection - services
        bottle.route('/services', 'GET', self.show_all_services)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/services', 'show published services'))
        bottle.route('/services.json', 'GET', self.services_json)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/services.json',
                'List published services in JSON format'))
        # show a specific service type
        bottle.route('/services/<service_type>', 'GET', self.show_all_services)

        # api to perform on-demand load-balance across available publishers
        bottle.route('/load-balance/<service_type>', 'POST', self.api_lb_service)

        # update service
        bottle.route('/service/<id>', 'PUT', self.service_http_put)

        # get service info
        bottle.route('/service/<id>', 'GET',  self.service_http_get)
        bottle.route('/service/<id>/brief', 'GET', self.service_brief_http_get)

        # delete (un-publish) service
        bottle.route('/service/<id>', 'DELETE', self.service_http_delete)

        # collection - clients
        bottle.route('/clients', 'GET', self.show_all_clients)
        bottle.route('/clients/<service_type>/<service_id>', 'GET', self.show_all_clients)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/clients', 'list all subscribers'))
        bottle.route('/clients.json', 'GET', self.clients_json)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/clients.json',
                'list all subscribers in JSON format'))

        # show config
        bottle.route('/config', 'GET', self.show_config)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/config', 'show discovery service config'))

        # show debug
        bottle.route('/stats', 'GET', self.show_stats)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/stats', 'show discovery service stats'))
        bottle.route('/stats.json', 'GET', self.api_stats_json)

        # cleanup
        bottle.route('/cleanup', 'GET', self.cleanup_http_get)
        self._homepage_links.append(LinkObject('action',
            self._base_url , '/cleanup', 'Purge inactive publishers'))

        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()

        # sandesh init
        self._sandesh = Sandesh()
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit( \
                self._args.sandesh_send_rate_limit)
        module = Module.DISCOVERY_SERVICE
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        self.table = "ObjectConfigNode"
        instance_id = self._args.worker_id
        disc_client = discovery_client.DiscoveryClient(
            self._args.listen_ip_addr, self._args.listen_port,
            ModuleNames[Module.DISCOVERY_SERVICE])
        self._sandesh.init_generator(
            module_name, socket.gethostname(), node_type_name, instance_id,
            self._args.collectors, 'discovery_context',
            int(self._args.http_server_port), ['discovery.sandesh'], disc_client,
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf)
        self._sandesh.set_logging_params(enable_local_log=self._args.log_local,
                                         category=self._args.log_category,
                                         level=self._args.log_level,
                                         enable_syslog=args.use_syslog,
                                         file=self._args.log_file)
        self._sandesh.trace_buffer_create(name="dsHeartBeatTraceBuf",
                                          size=1000)
        self._sandesh.trace_buffer_create(name="dsPublishTraceBuf",
                                          size=1000)
        self._sandesh.trace_buffer_create(name="dsSubscribeTraceBuf",
                                          size=1000)
        ConnectionState.init(self._sandesh, socket.gethostname(), module_name,
                instance_id, staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus, self.table)

        # DB interface initialization
        self._db_connect(self._args.reset_config)
        self._db_conn.db_update_service_entry_oper_state()
        self._db_conn.db_update_service_entry_admin_state()

        # build in-memory subscriber data
        self._sub_data = {}
        for (client_id, service_type) in self._db_conn.subscriber_entries():
            self.create_sub_data(client_id, service_type)

        # build white list
        if self._args.white_list_publish:
            self._pub_wl = IPSet()
            for prefix in self._args.white_list_publish.split(" "):
                self._pub_wl.add(prefix)
        if self._args.white_list_subscribe:
            self._sub_wl = IPSet()
            for prefix in self._args.white_list_subscribe.split(" "):
                self._sub_wl.add(prefix)

        self._auth_svc = None
        if self._args.auth == 'keystone':
            ks_conf = {
                'auth_host': self._args.auth_host,
                'auth_port': self._args.auth_port,
                'auth_protocol': self._args.auth_protocol,
                'admin_user': self._args.admin_user,
                'admin_password': self._args.admin_password,
                'admin_tenant_name': self._args.admin_tenant_name,
                'region_name': self._args.region_name,
            }
            self._auth_svc = disc_auth_keystone.AuthServiceKeystone(ks_conf)
    # end __init__

    def config_log(self, msg, level):
        self._sandesh.logger().log(SandeshLogger.get_py_logger_level(level),
                                   msg)

    def syslog(self, log_msg):
        log = sandesh.discServiceLog(
            log_msg=log_msg, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

    def create_sub_data(self, client_id, service_type):
        if not client_id in self._sub_data:
            self._sub_data[client_id] = {}
        if not service_type in self._sub_data[client_id]:
            sdata = {
                'ttl_expires': 0,
                'heartbeat': int(time.time()),
            }
            self._sub_data[client_id][service_type] = sdata
        return self._sub_data[client_id][service_type]
    # end

    def delete_sub_data(self, client_id, service_type):
        if (client_id in self._sub_data and
                service_type in self._sub_data[client_id]):
            del self._sub_data[client_id][service_type]
            if len(self._sub_data[client_id]) == 0:
                del self._sub_data[client_id]
    # end

    def get_sub_data(self, id, service_type):
        if id in self._sub_data and service_type in self._sub_data[id]:
            return self._sub_data[id][service_type]
        return self.create_sub_data(id, service_type)
    # end

    # Public Methods
    def get_args(self):
        return self._args
    # end get_args

    def get_ip_addr(self):
        return self._args.listen_ip_addr
    # end get_ip_addr

    def get_port(self):
        return self._args.listen_port
    # end get_port

    def get_pipe_start_app(self):
        return self._pipe_start_app
    # end get_pipe_start_app

    def homepage_http_get(self):
        json_links = []
        url = bottle.request.url[:-1]
        for link in self._homepage_links:
            json_links.append({'link': link.to_dict(with_url=url)})

        json_body = \
            {"href": self._base_url,
             "links": json_links
             }

        return json_body
    # end homepage_http_get


    def get_service_config(self, service_type, item):
        service = service_type.lower()
        if (service in self.service_config and
                item in self.service_config[service]):
            return self.service_config[service][item]
        elif item in self._args.__dict__:
            return self._args.__dict__[item]
        else:
            return None
    # end

    def _db_connect(self, reset_config):
        cred = None
        if 'cassandra' in self.cassandra_config.keys():
            cred = {'username':self.cassandra_config['cassandra']\
                    ['cassandra_user'],'password':self.cassandra_config\
                    ['cassandra']['cassandra_password']}
        self._db_conn = DiscoveryCassandraClient("discovery",
            self._args.cassandra_server_list, self.config_log, reset_config,
            self._args.cluster_id, cred)
    # end _db_connect

    def cleanup(self):
        pass
    # end cleanup

    def get_ttl_short(self, client_id, service_type, default):
        ttl = default
        if not client_id in self.short_ttl_map:
            self.short_ttl_map[client_id] = {}
        if service_type in self.short_ttl_map[client_id]:
            # keep doubling till we land in normal range
            ttl = self.short_ttl_map[client_id][service_type] * 2
            if ttl >= 32:
                ttl = 32

        self.short_ttl_map[client_id][service_type] = ttl
        return ttl
    # end

    # check if service expired (return color along)
    def service_expired(self, entry, include_color=False, include_down=True):
        timedelta = datetime.timedelta(
                seconds=(int(time.time()) - entry['heartbeat']))

        if self._args.hc_interval <= 0:
            # health check has been disabled
            color = "#00FF00"   # green - all good
            expired = False
        elif timedelta.seconds <= self._args.hc_interval:
            color = "#00FF00"   # green - all good
            expired = False
        elif (timedelta.seconds > (self._args.hc_interval *
                                   self._args.hc_max_miss)):
            color = "#FF0000"   # red - publication expired
            expired = True
        else:
            color = "#FFFF00"   # yellow - missed some heartbeats
            expired = False

        if include_down and \
                (entry['admin_state'] != 'up' or entry['oper_state'] != 'up'):
            color = "#FF0000"   # red - publication expired
            expired = True

        if include_color:
            return (expired, color, timedelta)
        else:
            return expired
    # end service_expired

    # decorator to catch DB error
    def db_error_handler(func):
        def error_handler(self, *args, **kwargs):
            try:
                return func(self, *args, **kwargs)
            except disc_exceptions.ServiceUnavailable:
                self._debug['503'] += 1
                bottle.abort(503, 'Service Unavailable')
            except Exception as e:
                self._debug['db_exc_unknown'] += 1
                self._debug['db_exc_info'] = str(sys.exc_info())
                raise
        return error_handler

    # decorator to authenticate request
    def authenticate(func):
        def wrapper(self, *args, **kwargs):
            if self._auth_svc and not self._auth_svc.is_admin(bottle.request):
                self._debug['auth_failures'] += 1
                bottle.abort(401, 'Unauthorized')
            return func(self, *args, **kwargs)
        return wrapper

    # 404 forces republish
    def heartbeat(self, sig):
        # self.syslog('heartbeat from "%s"' % sig)
        self._debug['msg_hbt'] += 1
        info = sig.split(':')
        if len(info) != 2:
            self.syslog('Unable to parse heartbeat cookie %s' % sig)
            bottle.abort(404, 'Unable to parse heartbeat')

        service_type = info[1]
        service_id = info[0]
        entry = self._db_conn.lookup_service(service_type, service_id)
        if not entry:
            self.syslog('Received stray heartbeat with cookie %s' % (sig))
            self._debug['hb_stray'] += 1
            bottle.abort(404, 'Publisher %s not found' % sig)

        # update heartbeat timestamp in database
        entry['heartbeat'] = int(time.time())

        # insert entry if timed out by background task
        self._db_conn.update_service(
                service_type, entry['service_id'], entry)

        m = sandesh.dsHeartBeat(
            publisher_id=sig, service_type=service_type,
            sandesh=self._sandesh)
        m.trace_msg(name='dsHeartBeatTraceBuf', sandesh=self._sandesh)
        return '200 OK'
    # end heartbeat

    @db_error_handler
    def api_heartbeat(self):
        ctype = bottle.request.headers['content-type']
        json_req = {}
        try:
            if 'application/xml' in ctype:
                data = xmltodict.parse(bottle.request.body.read())
            else:
                data = bottle.request.json
        except Exception as e:
            self.syslog('Unable to parse heartbeat')
            self.syslog(bottle.request.body.buf)
            bottle.abort(400, 'Unable to parse heartbeat')

        status = self.heartbeat(data['cookie'])
        return status

    @db_error_handler
    def api_publish(self, end_point = None):
        self._debug['msg_pubs'] += 1

        source = bottle.request.headers.get('X-Forwarded-For', None)
        if source and self._pub_wl and source not in self._pub_wl:
            self._debug['wl_rejects_pub'] += 1
            bottle.abort(401, 'Unauthorized request')

        ctype = bottle.request.headers['content-type']
        json_req = {}
        try:
            if 'application/json' in ctype:
                data = bottle.request.json
            elif 'application/xml' in ctype:
                data = xmltodict.parse(bottle.request.body.read())
        except Exception as e:
            self.syslog('Unable to parse publish request')
            self.syslog(bottle.request.body.buf)
            bottle.abort(415, 'Unable to parse publish request')

        # new format - publish tag to envelop entire content
        if 'publish' in data:
            data = data['publish']
        for key, value in data.items():
            json_req[key] = value

        # old format didn't include explicit tag for service type
        service_type = data.get('service-type', data.keys()[0])

        # convert ordered dict to normal dict
        try:
            json_req[service_type] = data[service_type]
        except (ValueError, KeyError, TypeError) as e:
            bottle.abort(400, "Unknown service type")

        info = json_req[service_type]
        remote = json_req.get('remote-addr',
                     bottle.request.environ['REMOTE_ADDR'])
        version = json_req.get('version', disc_consts.DEFAULT_VERSION)

        sig = json_req.get('service-id', end_point) or \
            publisher_id(remote, json.dumps(json_req))

        # Avoid reading from db unless service policy is fixed
        # Fixed policy is anchored on time of entry creation
        policy = self.get_service_config(service_type, 'policy')
        if policy == 'fixed':
            entry = self._db_conn.lookup_service(service_type, service_id=sig)
            if entry and self.service_expired(entry):
                entry = None
        else:
            entry = None

        if not entry:
            entry = {
                'service_type': service_type,
                'service_id': sig,
                'in_use': 0,
                'ts_use': 0,
                'ts_created': int(time.time()),
                'oper_state': 'up',
                'oper_state_msg': '',
                'sequence': str(int(time.time())) + socket.gethostname(),
            }

        if 'oper-state' in json_req:
            oper_state = json_req['oper-state']
            if oper_state not in ['up', 'down']:
                bottle.abort(400, "Invalid operational state")
            entry['oper_state'] = oper_state
        if 'oper-state-reason' in json_req:
            osr = json_req['oper-state-reason']
            if type(osr) != str and type(osr) != unicode:
                bottle.abort(400, "Invalid format of operational state reason")
            entry['oper_state_msg'] = osr

        entry['info'] = info
        entry['heartbeat'] = int(time.time())
        entry['remote'] = remote
        entry['version'] = version
        entry['ep_type'] = service_type
        entry['ep_id'] = sig

        # insert entry if new or timed out
        self._db_conn.update_service(service_type, sig, entry)

        response = {'cookie': sig + ':' + service_type}
        if ctype != 'application/json':
            response = xmltodict.unparse({'response': response})

        msg = 'service=%s, id=%s, info=%s' % (service_type, sig, json.dumps(info))
        m = sandesh.dsPublish(msg=msg, sandesh=self._sandesh)
        m.trace_msg(name='dsPublishTraceBuf', sandesh=self._sandesh)

        return response
    # end api_publish

    # randomize services with same in_use count to handle requests arriving
    # at the same time
    def disco_shuffle(self, list):
        if list is None or len(list) == 0:
            return list
        master_list = []
        working_list = []
        previous_use_count = list[0]['in_use']
        list.append({'in_use': -1})
        for item in list:
            if item['in_use'] != previous_use_count:
                random.shuffle(working_list)
                master_list.extend(working_list)
                working_list = []
            working_list.append(item)
            previous_use_count = item['in_use']
        return master_list

    # round-robin
    def service_list_round_robin(self, client_id, pubs):
        self._debug['policy_rr'] += 1
        return sorted(pubs, key=lambda service: service['ts_use'])
    # end

    # load-balance
    def service_list_load_balance(self, client_id, pubs):
        self._debug['policy_lb'] += 1
        temp = sorted(pubs, key=lambda service: service['in_use'])
        return self.disco_shuffle(temp)
    # end

    # master election
    def service_list_fixed(self, client_id, pubs):
        self._debug['policy_fi'] += 1
        return sorted(pubs, key=lambda service: service['sequence'])
    # end

    # consistent hash
    def service_list_chash(self, client_id, pubs):
        self._debug['policy_chash'] += 1
        pubs_sid = [pub['service_id'] for pub in pubs]
        pubs_dict = {pub['service_id']:pub for pub in pubs}
        ch = ConsistentHash(resource_list=pubs_sid, num_replicas=5)
        pubs = [pubs_dict[sid] for sid in ch.get_resources(client_id)]
        return pubs

    def service_list(self, client_id, service_type, pubs):
        policy = self.get_service_config(service_type, 'policy') or 'load-balance'

        if 'load-balance' in policy:
            f = self.service_list_load_balance
        elif policy == 'fixed':
            f = self.service_list_fixed
        elif policy == 'chash':
            f = self.service_list_chash
        else:
            f = self.service_list_round_robin

        return f(client_id, pubs)
    # end

    """
    read DSA rules
    'dsa_rule_entry': {
        u'subscriber': [{
            u'ep_type': u'',
            u'ep_id': u'',
            u'ep_prefix': {u'ip_prefix': u'2.2.2.2', u'ip_prefix_len': 32},
            u'ep_version': u''}],
        u'publisher': {
            u'ep_type': u'',
            u'ep_id': u'',
            u'ep_prefix': {u'ip_prefix': u'1.1.1.1', u'ip_prefix_len': 32},
            u'ep_version': u''}
    }
    """
    def match_dsa_rule_ep(self, rule_ep, ep):
        # self.syslog('dsa rule_ep %s, ep %s' % (rule_ep, ep))

        if rule_ep['ep_type'] != '' and rule_ep['ep_type'] != ep['ep_type']:
            return (False, 0)
        if rule_ep['ep_id'] != '' and rule_ep['ep_id'] != ep['ep_id']:
            return (False, 0)
        if rule_ep['ep_version'] != '' and rule_ep['ep_version'] != ep['version']:
            return (False, 0);

        prefix = rule_ep['ep_prefix']
        if prefix['ip_prefix'] == '':
            return (True, 0)

        network = IPNetwork('%s/%s' % (prefix['ip_prefix'], prefix['ip_prefix_len']))
        remote = IPAddress(ep['remote'])
        return (remote in network, prefix['ip_prefix_len'])

    def match_subscriber(self, dsa_rule, sub):
        for rule_sub in dsa_rule['subscriber']:
            match, match_len = self.match_dsa_rule_ep(rule_sub, sub)
            if match:
                return True, match_len
        return False, 0

    def match_publishers(self, rule_list, pubs):
        result = []

        for pub in pubs:
            for dsa_rule in rule_list:
                ok, pfxlen = self.match_dsa_rule_ep(dsa_rule['publisher'], pub)
                if ok:
                    result.append(pub)
        return result

    def apply_dsa_config(self, service_type, pubs, sub):
        if len(pubs) == 0:
            return pubs

        dsa_rules = self._db_conn.read_dsa_config()
        if dsa_rules is None:
            return pubs
        # self.syslog('dsa: rules %s' % dsa_rules)

        lpm = -1
        matched_rules = None
        for rule in dsa_rules:
            # ignore rule if publisher not relevant
            if rule['publisher']['ep_type'] != service_type:
                continue

            # ignore rule if subscriber doesn't match
            matched, matched_len = self.match_subscriber(rule, sub)
            if not matched:
                continue
            self.syslog('dsa: matched sub %s' % sub)

            # collect matched rules
            if matched_len > lpm:
                lpm = matched_len
                matched_rules = [rule]
            elif matched_len == lpm:
                matched_rules.append(rule)
        # end for

        # return original list if there is no sub match
        if not matched_rules:
            return pubs

        matched_pubs = self.match_publishers(matched_rules, pubs)
        self.syslog('dsa: matched pubs %s' % matched_pubs)

        return matched_pubs

    # reorder services (pubs) based on what client is actually using (in_use_list)
    # client's in-use list publishers are pushed to the top
    def adjust_in_use_list(self, pubs, in_use_list):
        if not in_use_list:
            return pubs
        pubid_list = in_use_list['publisher-id']
        if not pubid_list:
            return pubs

        pubs_dict = {entry['service_id']:entry for entry in pubs}
        result = [pubs_dict[pid] for pid in pubid_list if pid in pubs_dict]
        x = [entry for entry in pubs if entry['service_id'] not in pubid_list]
        return result + x

    @db_error_handler
    def api_subscribe(self):
        self._debug['msg_subs'] += 1

        source = bottle.request.headers.get('X-Forwarded-For', None)
        if source and self._sub_wl and source not in self._sub_wl:
            self._debug['wl_rejects_sub'] += 1
            bottle.abort(401, 'Unauthorized request')

        ctype = bottle.request.headers['content-type']
        if 'application/json' in ctype:
            json_req = bottle.request.json
        elif 'application/xml' in ctype:
            data = xmltodict.parse(bottle.request.body.read())
            json_req = {}
            for service_type, info in data.items():
                json_req['service'] = service_type
                json_req.update(dict(info))
        else:
            bottle.abort(400, e)

        service_type = json_req['service']
        client_id = json_req['client']
        count = reqcnt = int(json_req['instances'])
        client_type = json_req.get('client-type', '')
        remote = json_req.get('remote-addr',
                     bottle.request.environ['REMOTE_ADDR'])
        version = json_req.get('version', disc_consts.DEFAULT_VERSION)

        assigned_sid = set()
        r = []
        ttl_min = int(self.get_service_config(service_type, 'ttl_min'))
        ttl_max = int(self.get_service_config(service_type, 'ttl_max'))
        ttl = random.randint(ttl_min, ttl_max)


        # check client entry and any existing subscriptions
        cl_entry, subs = self._db_conn.lookup_client(service_type, client_id)
        if not cl_entry:
            cl_entry = {
                'instances': count,
                'client_type': client_type,
                'client_id': client_id,
            }
            self.create_sub_data(client_id, service_type)
        # honor one-time load-balance if enabled OOB
        lb_oob = cl_entry.get('load-balance', None)
        cl_entry['load-balance'] = None
        cl_entry['remote'] = remote
        cl_entry['version'] = version
        cl_entry['ep_type'] = client_type
        cl_entry['ep_id'] = client_id
        self._db_conn.insert_client_data(service_type, client_id, cl_entry)

        sdata = self.get_sub_data(client_id, service_type)
        if sdata:
            sdata['ttl_expires'] += 1

        # send short ttl if no publishers
        pubs = self._db_conn.lookup_service(service_type, include_count=True) or []
        pubs_active = [item for item in pubs if not self.service_expired(item)]
        pubs_active = self.apply_dsa_config(service_type, pubs_active, cl_entry)
        pubs_active = self.service_list(client_id, service_type, pubs_active)
        plist = dict((entry['service_id'],entry) for entry in pubs_active)
        plist_all = dict((entry['service_id'],entry) for entry in pubs)

        try:
            inuse_list = json_req['service-in-use-list']['publisher-id']
            # convert to list if singleton
            if type(inuse_list) != list:
                inuse_list = [inuse_list]
        except Exception as e:
            inuse_list = []

        min_instances = int(json_req.get('min-instances', '0'))
        assign = count or max(len(inuse_list), min_instances)

        if len(pubs_active) < count or len(pubs_active) < min_instances:
            ttl = random.randint(1, 32)
            self._debug['ttl_short'] += 1

        cid = "<cl=%s,st=%s>" % (client_id, service_type)
        msg = '%s, ttl=%d, asked=%d pubs=%d/%d, subs=%d, m_i=%d, assign=%d, siul=%d' \
            % (cid, ttl, count, len(pubs), len(pubs_active), len(subs),
            min_instances, assign, len(inuse_list))
        m = sandesh.dsSubscribe(msg=msg, sandesh=self._sandesh)
        m.trace_msg(name='dsSubscribeTraceBuf', sandesh=self._sandesh)

        if count == 0:
            count = len(pubs_active)

        policy = self.get_service_config(service_type, 'policy')

        # Auto load-balance is triggered if enabled and some servers are
        # more than 5% off expected average allocation. If multiple spots
        # are impacted, we pick one randomly for replacement
        if len(subs) and (policy == 'dynamic-load-balance' or lb_oob == "partial"):
            total_subs = sum([entry['in_use'] for entry in pubs_active])
            avg = total_subs/len(pubs_active)
            impacted = [entry['service_id'] for entry in pubs_active if entry['in_use'] > int(1.05*avg)]
            candidate_list = [(index, item[0]) for index, item in enumerate(subs) if item[0] in impacted]
            if (len(candidate_list) > 0):
                index = random.randint(0, len(candidate_list) - 1)
                msg = "impacted %s, candidate_list %s, replace %d, total_subs=%d, avg=%f" %\
                    (impacted, candidate_list, index, total_subs, avg)
                m = sandesh.dsSubscribe(msg=msg, sandesh=self._sandesh)
                m.trace_msg(name='dsSubscribeTraceBuf', sandesh=self._sandesh)
                self.syslog(msg)
                replace_candidate = candidate_list[index]
                subs[replace_candidate[0]] = (replace_candidate[1], True)
                self._debug['lb_auto'] += 1
        elif len(subs) and (lb_oob == "full" or policy == 'chash'):
           subs = [(service_id, True) for service_id, expiry in subs]

        # if subscriber in-use-list present, forget previous assignments
        if len(inuse_list):
            expiry_dict = dict((service_id,expiry) for service_id, expiry in subs or [])
            subs = [(service_id, expiry_dict.get(service_id, False)) for service_id in inuse_list]

        if subs and count:
            for service_id, expired in subs:
                # expired True if service was marked for deletion by LB command
                # previously published service is gone
                # force renew for fixed policy since some service may have flapped
                entry = plist.get(service_id, None)
                if entry is None or expired or policy == 'fixed':
                    # purge previous assignment
                    self.syslog("%s del sid %s, policy=%s, expired=%s" % (cid, service_id, policy, expired))
                    self._db_conn.delete_subscription(service_type, client_id, service_id)
                    if policy == 'fixed':
                        continue
                    # replace publisher
                    if len(pubs_active) == 0:
                        continue
                    entry = pubs_active.pop(0)
                    service_id = entry['service_id']
                # skip inadvertent duplicate
                if service_id in assigned_sid:
                    continue
                msg = ' subs service=%s, assign=%d, count=%d' % (service_id, assign, count)
                m = sandesh.dsSubscribe(msg=msg, sandesh=self._sandesh)
                m.trace_msg(name='dsSubscribeTraceBuf', sandesh=self._sandesh)
                self.syslog("%s %s" % (cid, msg))

                if assign:
                    assign -= 1
                    self._db_conn.insert_client(
                        service_type, service_id, client_id, entry['info'], ttl)
                r.append(entry)
                assigned_sid.add(service_id)
                count -= 1
                if count == 0:
                    break

        # skip duplicates from existing assignments
        pubs = [entry for entry in pubs_active if not entry['service_id'] in assigned_sid]

        # take first 'count' publishers
        for entry in pubs[:min(count, len(pubs))]:
            r.append(entry)

            msg = ' assign service=%s, info=%s' % \
                        (entry['service_id'], json.dumps(entry['info']))
            m = sandesh.dsSubscribe(msg=msg, sandesh=self._sandesh)
            m.trace_msg(name='dsSubscribeTraceBuf', sandesh=self._sandesh)
            self.syslog("%s %s" % (cid, msg))

            # create client entry
            if assign:
                assign -= 1
                self._db_conn.insert_client(
                    service_type, entry['service_id'], client_id, entry['info'], ttl)

            # update publisher TS for round-robin algorithm
            entry['ts_use'] = self._ts_use
            self._ts_use += 1
            self._db_conn.update_service(
                service_type, entry['service_id'], entry)


        pubs = []
        for entry in r:
            result = entry['info'].copy()
            result['@publisher-id'] = entry['service_id']
            pubs.append(result)
        response = {'ttl': ttl, service_type: pubs}
        if 'application/xml' in ctype:
            response = xmltodict.unparse({'response': response})
        return response
    # end api_subscribe

    # on-demand API to load-balance existing subscribers across all currently available
    # publishers. Needed if publisher gets added or taken down
    @authenticate
    def api_lb_service(self, service_type):
        if service_type is None:
            bottle.abort(405, "Missing service")

        # load-balance type can be partial or full. Partial replaces one
        # publisher randomly while latter replaces all.
        lb_type = "partial"
        try:
            json_req = bottle.request.json
            lb_type = json_req['type'].lower()
        except Exception as e:
            pass
        self.syslog("oob load-balance: %s" % lb_type)
        self._debug['lb_%s' % lb_type] += 1
        lb_partial = (lb_type == "partial")

        clients = self._db_conn.get_all_clients(service_type = service_type, 
                      unique_clients = lb_partial)
        if clients is None:
            return

        # enable one-time load-balancing event for clients of service_type (ref: lb_oob)
        for client in clients:
            if lb_partial:
                (service_type, client_id, cl_entry) = client
                cl_entry['load-balance'] = lb_type
                self._db_conn.insert_client_data(service_type, client_id, cl_entry)
                self.syslog('load-balance client=%s, service=%s' % (client_id, service_type))
            else:
                (service_type, client_id, service_id, mtime, ttl) = client
                self._db_conn.delete_subscription(service_type, client_id, service_id)
                self.syslog('expire client=%s, service=%s' % (client_id, service_id))
        return {}
    # end api_lb_service

    def api_query(self):
        self._debug['msg_query'] += 1
        ctype = bottle.request.headers['content-type']
        if ctype == 'application/json':
            json_req = bottle.request.json
        elif ctype == 'application/xml':
            data = xmltodict.parse(bottle.request.body.read())
            json_req = {}
            for service_type, info in data.items():
                json_req['service'] = service_type
                json_req.update(dict(info))
        else:
            bottle.abort(400, e)

        service_type = json_req['service']
        count = int(json_req['instances'])

        r = []

        # lookup publishers of the service
        pubs = self._db_conn.query_service(service_type)
        if not pubs:
            return {service_type: r}

        # eliminate inactive services
        pubs_active = [item for item in pubs if not self.service_expired(item)]
        self.syslog(' query: Found %s publishers, %d active, need %d' %
                    (len(pubs), len(pubs_active), count))

        # find least loaded instances
        pubs = pubs_active

        # prepare response - send all if count 0
        for index in range(min(count, len(pubs)) if count else len(pubs)):
            entry = pubs[index]

            result = entry['info']
            r.append(result)
            self.syslog(' assign service=%s, info=%s' %
                        (entry['service_id'], json.dumps(result)))

        response = {service_type: r}
        if 'application/xml' in ctype:
            response = xmltodict.unparse({'response': response})
        return response
    # end api_query

    @db_error_handler
    def show_all_services(self, service_type=None):

        rsp = output.display_user_menu()
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Service Type</td>\n'
        rsp += '        <td>Remote IP</td>\n'
        rsp += '        <td>Service Id</td>\n'
        rsp += '        <td>Oper State</td>\n'
        rsp += '        <td>Admin State</td>\n'
        rsp += '        <td>In Use</td>\n'
        rsp += '        <td>Time since last Heartbeat</td>\n'
        rsp += '    </tr>\n'

        for pub in self._db_conn.lookup_service(service_type, include_count=True):
            info = pub['info']
            service_type = pub['service_type']
            service_id   = pub['service_id']
            sig = service_id + ':' + service_type
            rsp += '    <tr>\n'
            if service_type:
                rsp += '        <td>' + service_type + '</td>\n'
            else:
                link = do_html_url("/services/%s" % service_type, service_type)
                rsp += '        <td>' + link + '</td>\n'
            rsp += '        <td>' + pub['remote'] + '</td>\n'
            link = do_html_url("/service/%s/brief" % sig, sig)
            rsp += '        <td>' + link + '</td>\n'
            oper_state_str = pub['oper_state']
            if oper_state_str == 'down' and len(pub['oper_state_msg']) > 0:
                oper_state_str += '/' + pub['oper_state_msg']
            rsp += '        <td>' + oper_state_str + '</td>\n'
            rsp += '        <td>' + pub['admin_state'] + '</td>\n'
            link = do_html_url("/clients/%s/%s" % (service_type, service_id),
                str(pub['in_use']))
            rsp += '        <td>' + link + '</td>\n'
            (expired, color, timedelta) = self.service_expired(
                pub, include_color=True)
            #status = "down" if expired else "up"
            rsp += '        <td bgcolor=%s>' % (
                color) + str(timedelta) + '</td>\n'
            rsp += '    </tr>\n'
        rsp += ' </table>\n'

        return rsp
    # end show_services

    @db_error_handler
    def services_json(self, service_type=None):
        rsp = []

        for pub in self._db_conn.lookup_service(service_type, include_count=True):
            entry = pub.copy()
            entry['status'] = "down" if self.service_expired(entry) else "up"
            # keep sanity happy - get rid of prov_state in the future
            entry['prov_state'] = entry['oper_state']
            entry['hbcount'] = 0
            # send unique service ID (hash or service endpoint + type)
            entry['service_id'] = str(entry['service_id'] + ':' + entry['service_type'])
            rsp.append(entry)
        return {'services': rsp}
    # end services_json

    @authenticate
    def service_http_put(self, id):
        self.syslog('Update service %s' % (id))
        try:
            json_req = bottle.request.json
            service_type = json_req['service-type']
            self.syslog('Entry %s' % (json_req))
        except (ValueError, KeyError, TypeError) as e:
            bottle.abort(400, e)

        # admin state is kept seperately and can be set before publish
        if 'admin-state' in json_req:
            admin_state = json_req['admin-state']
            if admin_state not in ['up', 'down']:
                bottle.abort(400, "Invalid admin state")
            self._db_conn.set_admin_state(service_type, id, admin_state)

        # oper state is kept in main db entry for publisher
        if 'oper-state' in json_req or 'oper-state-reason' in json_req:
            entry = self._db_conn.lookup_service(service_type, service_id=id)
            if not entry:
                bottle.abort(405, 'Unknown service')
            if 'oper-state' in json_req:
                oper_state = json_req['oper-state']
                if oper_state not in ['up', 'down']:
                    bottle.abort(400, "Invalid operational state")
                entry['oper_state'] = oper_state
            if 'oper-state-reason' in json_req:
                entry['oper_state_msg'] = json_req['oper-state-reason']
            self._db_conn.update_service(service_type, id, entry)
            self.syslog('update service=%s, sid=%s, info=%s'
                    % (service_type, id, entry))

        return {}
    # end service_http_put

    def service_http_delete(self, id):
        info = id.split(':')
        service_type = info[1]
        service_id = info[0]
        self.syslog('Delete service %s:%s' % (service_id, service_type))
        entry = self._db_conn.lookup_service(service_type, service_id)
        if not entry:
            bottle.abort(405, 'Unknown service')

        self._db_conn.delete_service(entry)

        self.syslog('delete service=%s, sid=%s, info=%s'
                    % (service_type, service_id, entry))

        return {}
    # end service_http_delete

    # return service info - meta as well as published data
    def service_http_get(self, id):
        info = id.split(':')
        service_type = info[1]
        service_id = info[0]
        pub = self._db_conn.lookup_service(service_type, service_id)
        if pub:
            entry = pub.copy()
            entry['hbcount'] = 0
            entry['status'] = "down" if self.service_expired(entry) else "up"
            # keep sanity happy - get rid of prov_state in the future
            entry['prov_state'] = entry['oper_state']

        return entry
    # end service_http_get

    # return service info - only published data
    def service_brief_http_get(self, id):
        info = id.split(':')
        service_type = info[1]
        service_id = info[0]
        entry = self._db_conn.lookup_service(service_type, service_id)
        if entry:
            return entry['info']
        else:
            return 'Unknown service %s' % id
    # end service_brief_http_get

    # purge expired publishers
    def cleanup_http_get(self):
        for entry in self._db_conn.lookup_service():
            if self.service_expired(entry):
                self._db_conn.delete_service(entry)
        return self.show_all_services()
    #end

    @db_error_handler
    def show_all_clients(self, service_type=None, service_id=None):

        rsp = output.display_user_menu()
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Client IP</td>\n'
        rsp += '        <td>Client Type</td>\n'
        rsp += '        <td>Client Id</td>\n'
        rsp += '        <td>Service Type</td>\n'
        rsp += '        <td>Service Id</td>\n'
        rsp += '        <td>TTL (sec)</td>\n'
        rsp += '        <td>Time Remaining</td>\n'
        rsp += '        <td>Refresh Count</td>\n'
        rsp += '    </tr>\n'

        # lookup subscribers of the service
        clients = self._db_conn.get_all_clients(service_type=service_type,
            service_id=service_id)

        if not clients:
            return rsp

        for client in clients:
            (service_type, client_id, service_id, mtime, ttl) = client
            cl_entry, subs = self._db_conn.lookup_client(service_type, client_id)
            if cl_entry is None:
                continue
            sdata = self.get_sub_data(client_id, service_type)
            if sdata is None:
                self.syslog('Missing sdata for client %s, service %s' %
                            (client_id, service_type))
                continue
            rsp += '    <tr>\n'
            rsp += '        <td>' + cl_entry['remote'] + '</td>\n'
            client_type = cl_entry.get('client_type', '')
            rsp += '        <td>' + client_type + '</td>\n'
            rsp += '        <td>' + client_id + '</td>\n'
            rsp += '        <td>' + service_type + '</td>\n'
            sig = service_id + ':' + service_type
            link = do_html_url("service/%s/brief" % (sig), sig)
            rsp += '        <td>' + link + '</td>\n'
            rsp += '        <td>' + str(ttl) + '</td>\n'
            remaining = ttl - int(time.time() - mtime)
            rsp += '        <td>' + str(remaining) + '</td>\n'
            rsp += '        <td>' + str(sdata['ttl_expires']) + '</td>\n'
            rsp += '    </tr>\n'
        rsp += ' </table>\n'

        return rsp
    # end show_clients

    @db_error_handler
    def clients_json(self):

        rsp = []
        clients = self._db_conn.get_all_clients()

        if not clients:
            return {'services': rsp}

        for client in clients:
            (service_type, client_id, service_id, mtime, ttl) = client
            cl_entry, subs = self._db_conn.lookup_client(service_type, client_id)
            sdata = self.get_sub_data(client_id, service_type)
            if sdata is None:
                self.syslog('Missing sdata for client %s, service %s' %
                            (client_id, service_type))
                continue
            entry = cl_entry.copy()
            entry.update(sdata)

            entry['client_id'] = str(client_id)
            entry['service_type'] = str(service_type)
            # send unique service ID (hash or service endpoint + type)
            entry['service_id'] = str(service_id + ':' + service_type)
            entry['ttl'] = ttl
            rsp.append(entry)

        return {'services': rsp}
    # end show_clients

    def show_config(self):
        """
        r = {}
        r['global'] = self._args.__dict__
        for service, config in self.service_config.items():
            r[service] = config
        return r
        """

        rsp = output.display_user_menu()

        #rsp += '<h4>Defaults:</h4>'
        rsp += '<table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '<tr><th colspan="2">Defaults</th></tr>'
        for k in sorted(self._args.__dict__.iterkeys()):
            v = self._args.__dict__[k]
            rsp += '<tr><td>%s</td><td>%s</td></tr>' % (k, v)
        rsp += '</table>'
        rsp += '<br>'

        for service, config in self.service_config.items():
            #rsp += '<h4>%s:</h4>' %(service)
            rsp += '<table border="1" cellpadding="1" cellspacing="0">\n'
            rsp += '<tr><th colspan="2">%s</th></tr>' % (service)
            for k in sorted(config.iterkeys()):
                rsp += '<tr><td>%s</td><td>%s</td></tr>' % (k, config[k])
            rsp += '</table>'
            rsp += '<br>'
        return rsp
    # end show_config

    def show_stats(self):
        stats = self._debug
        stats.update(self._db_conn.get_debug_stats())

        rsp = output.display_user_menu()
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        for k in sorted(stats.iterkeys()):
            rsp += '    <tr>\n'
            rsp += '        <td>%s</td>\n' % (k)
            rsp += '        <td>%s</td>\n' % (stats[k])
            rsp += '    </tr>\n'
        return rsp
    # end show_stats

    def api_stats_json(self):
        stats = self._debug
        stats.update(self._db_conn.get_debug_stats())
        return stats
    # end show_stats_json

# end class DiscoveryServer

def parse_args(args_str):
    '''
    Eg. python discovery.py

                 --cass_server_ip 10.1.2.3
                 --cass_server_port 9160
                 --listen_ip_addr 127.0.0.1
                 --listen_port 5998
                 --use_syslog
                 --worker_id 1
    '''

    # Source any specified config/ini file
    # Turn off help, so we print all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)
    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    defaults = {
        'reset_config': False,
        'listen_ip_addr': disc_consts._WEB_HOST,
        'listen_port': disc_consts._WEB_PORT,
        'cass_server_ip': disc_consts._CASSANDRA_HOST,
        'cass_server_port': disc_consts._CASSANDRA_PORT,
        'cass_max_retries': disc_consts._CASSANDRA_MAX_RETRIES,
        'cass_timeout': disc_consts._CASSANDRA_TIMEOUT,
        'ttl_min': disc_consts._TTL_MIN,
        'ttl_max': disc_consts._TTL_MAX,
        'ttl_short': 0,
        'hc_interval': disc_consts.HC_INTERVAL,
        'hc_max_miss': disc_consts.HC_MAX_MISS,
        'collectors': None,
        'http_server_port': '5997',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'use_syslog': False,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'worker_id': '0',
        'logging_conf': '',
        'logger_class': None,
        'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
        'cluster_id': None,
        'white_list_publish': None,
        'white_list_subscribe': None,
        'policy': 'load-balance',
    }

    cassandra_opts = {
        'cassandra_user'     : None,
        'cassandra_password' : None,
    }
    keystone_opts = {
        'auth_host': '127.0.0.1',
        'auth_port': '35357',
        'auth_protocol': 'http',
        'admin_user': '',
        'admin_password': '',
        'admin_tenant_name': '',
        'region_name': 'RegionOne',
    }

    service_config = {}
    cassandra_config = {}

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(cassandra_opts)
        for section in config.sections():
            if section == "CASSANDRA":
                cassandra_config[section.lower()] = cassandra_opts.copy()
                cassandra_config[section.lower()].update(
                    dict(config.items(section)))
                continue
            if section == "DEFAULTS":
                defaults.update(dict(config.items("DEFAULTS")))
                continue
            if section == "KEYSTONE":
                keystone_opts.update(dict(config.items("KEYSTONE")))
                continue
            service = section.lower()
            service_config[service] = dict(config.items(section))

    defaults.update(keystone_opts)
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--cass_max_retries", type=int,
        help="Maximum number of request retry, default %d"
        % (disc_consts._CASSANDRA_MAX_RETRIES))
    parser.add_argument(
        "--cass_timeout", type=float,
        help="Timeout of request, default %d"
        % (disc_consts._CASSANDRA_TIMEOUT))
    parser.add_argument(
        "--reset_config", action="store_true",
        help="Warning! Destroy previous configuration and start clean")
    parser.add_argument(
        "--listen_ip_addr",
        help="IP address to provide service on, default %s"
        % (disc_consts._WEB_HOST))
    parser.add_argument(
        "--listen_port", type=int,
        help="Port to provide service on, default %s"
        % (disc_consts._WEB_PORT))
    parser.add_argument(
        "--ttl_min", type=int,
        help="Minimum time to cache service information, default %d"
        % (disc_consts._TTL_MIN))
    parser.add_argument(
        "--ttl_max", type=int,
        help="Maximum time to cache service information, default %d"
        % (disc_consts._TTL_MAX))
    parser.add_argument(
        "--ttl_short", type=int,
        help="Short TTL for agressively subscription schedule")
    parser.add_argument(
        "--hc_interval", type=int,
        help="Heartbeat interval, default %d seconds"
        % (disc_consts.HC_INTERVAL))
    parser.add_argument(
        "--hc_max_miss", type=int,
        help="Maximum heartbeats to miss before declaring out-of-service, "
        "default %d" % (disc_consts.HC_MAX_MISS))
    parser.add_argument("--collectors",
        help="List of VNC collectors in ip:port format",
        nargs="+")
    parser.add_argument("--http_server_port",
        help="Port of local HTTP server")
    parser.add_argument(
        "--log_local", action="store_true",
        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument(
        "--use_syslog", action="store_true",
        help="Use syslog for logging")
    parser.add_argument("--log_file",
        help="Filename for the logs to be written to")
    parser.add_argument(
        "--worker_id",
        help="Worker Id")
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))
    parser.add_argument("--cassandra_user",
            help="Cassandra user name")
    parser.add_argument("--cassandra_password",
            help="Cassandra password")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec")
    parser.add_argument("--cluster_id",
            help="Used for database keyspace separation")
    parser.add_argument(
        "--auth", choices=['keystone'],
        help="Type of authentication for user-requests")

    args = parser.parse_args(remaining_argv)
    args.conf_file = args.conf_file
    args.service_config = service_config
    args.cassandra_config = cassandra_config
    args.cassandra_opts = cassandra_opts
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list =\
            args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    return args
# end parse_args

server = None
def run_discovery_server(args):
    global server
    server = DiscoveryServer(args)
    pipe_start_app = server.get_pipe_start_app()

    try:
        bottle.run(app=pipe_start_app, host=server.get_ip_addr(),
                   port=server.get_port(), server='gevent')
    except Exception as e:
        # cleanup gracefully
        server.cleanup()
#end run_discovery_server

def main(args_str=None):
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    run_discovery_server(args)
# end main

def server_main():
    import cgitb
    cgitb.enable(format='text')

    main()
# end server_main

if __name__ == "__main__":
    server_main()
