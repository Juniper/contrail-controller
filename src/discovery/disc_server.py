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

import bottle

from disc_utils import *
import disc_consts
import disc_exceptions
import output
import discoveryclient.client as discovery_client

# sandesh
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames,\
    INSTANCE_ID_DEFAULT    
from sandesh.discovery_introspect import ttypes as sandesh

from gevent.coros import BoundedSemaphore
from cfgm_common.rest import LinkObject


def obj_to_json(obj):
    # Non-null fields in object get converted to json fields
    return lambda obj: dict((k, v) for k, v in obj.__dict__.iteritems())
# end obj_to_json

class DiscoveryServer():

    def __init__(self, args):
        self._homepage_links = []
        self._args = args
        self.service_config = args.service_config
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
            'db_upd_hb': 0,
            'throttle_subs':0,
            '503': 0,
            'count_lb': 0,
        }
        self._ts_use = 1
        self.short_ttl_map = {}
        self._sem = BoundedSemaphore(1)

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
        instance_id = self._args.worker_id
        disc_client = discovery_client.DiscoveryClient(
            '127.0.0.1', self._args.listen_port,
            ModuleNames[Module.DISCOVERY_SERVICE])
        self._sandesh.init_generator(
            module_name, socket.gethostname(), node_type_name, instance_id,
            self._args.collectors, 'discovery_context', 
            int(self._args.http_server_port), ['sandesh'], disc_client,
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf)
        self._sandesh.set_logging_params(enable_local_log=self._args.log_local,
                                         category=self._args.log_category,
                                         level=self._args.log_level,
                                         enable_syslog=args.use_syslog,
                                         file=self._args.log_file)
        self._sandesh.trace_buffer_create(name="dsHeartBeatTraceBuf",
                                          size=1000)

        # DB interface initialization
        self._db_connect(self._args.reset_config)
        self._db_conn.db_update_service_entry_oper_state()

        # build in-memory subscriber data
        self._sub_data = {}
        for (client_id, service_type) in self._db_conn.subscriber_entries():
            self.create_sub_data(client_id, service_type)
    # end __init__

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
        self._db_conn = DiscoveryCassandraClient("discovery",
            self._args.cassandra_server_list, reset_config,
            self._args.cass_max_retries,
            self._args.cass_timeout)
    # end _db_connect

    def cleanup(self):
        pass
    # end cleanup

    def syslog(self, log_msg):
        log = sandesh.discServiceLog(
            log_msg=log_msg, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

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
                raise
        return error_handler

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

        sig = end_point or publisher_id(remote, json.dumps(json_req))

        entry = self._db_conn.lookup_service(service_type, service_id=sig)
        if not entry:
            entry = {
                'service_type': service_type,
                'service_id': sig,
                'in_use': 0,
                'ts_use': 0,
                'ts_created': int(time.time()),
                'oper_state': 'up',
                'admin_state': 'up',
                'oper_state_msg': '',
                'sequence': str(int(time.time())) + socket.gethostname(),
            }
        elif 'sequence' not in entry or self.service_expired(entry):
            # handle upgrade or republish after expiry
            entry['sequence'] = str(int(time.time())) + socket.gethostname()

        if 'admin-state' in json_req:
            admin_state = json_req['admin-state']
            if admin_state not in ['up', 'down']:
                bottle.abort(400, "Invalid admin state")
            entry['admin_state'] = admin_state
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

        # insert entry if new or timed out
        self._db_conn.update_service(service_type, sig, entry)

        response = {'cookie': sig + ':' + service_type}
        if ctype != 'application/json':
            response = xmltodict.unparse({'response': response})

        self.syslog('publish service "%s", sid=%s, info=%s'
                    % (service_type, sig, info))

        if not service_type.lower() in self.service_config:
            self.service_config[
                service_type.lower()] = self._args.default_service_opts

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
    def service_list_round_robin(self, pubs):
        self._debug['policy_rr'] += 1
        return sorted(pubs, key=lambda service: service['ts_use'])
    # end

    # load-balance
    def service_list_load_balance(self, pubs):
        self._debug['policy_lb'] += 1
        temp = sorted(pubs, key=lambda service: service['in_use'])
        return self.disco_shuffle(temp)
    # end

    # master election
    def service_list_fixed(self, pubs):
        self._debug['policy_fi'] += 1
        return sorted(pubs, key=lambda service: service['sequence'])
    # end

    def service_list(self, service_type, pubs):
        policy = self.get_service_config(service_type, 'policy') or 'load-balance'

        if policy == 'load-balance':
            f = self.service_list_load_balance
        elif policy == 'fixed':
            f = self.service_list_fixed
        else:
            f = self.service_list_round_robin

        return f(pubs)
    # end

    @db_error_handler
    def api_subscribe(self):
        self._debug['msg_subs'] += 1
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
            }
            self.create_sub_data(client_id, service_type)
        cl_entry['remote'] = remote
        self._db_conn.insert_client_data(service_type, client_id, cl_entry)

        sdata = self.get_sub_data(client_id, service_type)
        if sdata:
            sdata['ttl_expires'] += 1

        # send short ttl if no publishers
        pubs = self._db_conn.lookup_service(service_type) or []
        pubs_active = [item for item in pubs if not self.service_expired(item)]
        if len(pubs_active) < count:
            ttl = random.randint(1, 32)
            self._debug['ttl_short'] += 1

        self.syslog(
            'subscribe: service type=%s, client=%s:%s, ttl=%d, asked=%d pubs=%d/%d, subs=%d'
            % (service_type, client_type, client_id, ttl, count,
            len(pubs), len(pubs_active), len(subs)))

        # handle query for all publishers
        if count == 0:
            r = [entry['info'] for entry in pubs_active]
            response = {'ttl': ttl, service_type: r}
            if 'application/xml' in ctype:
                response = xmltodict.unparse({'response': response})
            return response

        if subs:
            plist = dict((entry['service_id'],entry) for entry in pubs_active)
            plist_all = dict((entry['service_id'],entry) for entry in pubs)
            policy = self.get_service_config(service_type, 'policy')
            for service_id, expired in subs:
                # expired True if service was marked for deletion by LB command
                # previously published service is gone
                # force renew for fixed policy since some service may have flapped
                entry2 = plist_all.get(service_id, None)
                entry = plist.get(service_id, None)
                if entry is None or expired or policy == 'fixed':
                    self._db_conn.delete_subscription(service_type, client_id, service_id)
                    # delete fixed policy server if expired
                    if policy == 'fixed' and entry is None and entry2:
                        self._db_conn.delete_service(entry2)
                    continue
                result = entry['info']
                self._db_conn.insert_client(
                    service_type, service_id, client_id, result, ttl)
                r.append(result)
                assigned_sid.add(service_id)
                count -= 1
                if count == 0:
                    response = {'ttl': ttl, service_type: r}
                    if 'application/xml' in ctype:
                        response = xmltodict.unparse({'response': response})
                    return response


        # skip duplicates from existing assignments
        pubs = [entry for entry in pubs_active if not entry['service_id'] in assigned_sid]

        # find instances based on policy (lb, rr, fixed ...)
        pubs = self.service_list(service_type, pubs)

        # take first 'count' publishers
        for entry in pubs[:min(count, len(pubs))]:
            result = entry['info']
            r.append(result)

            self.syslog(' assign service=%s, info=%s' %
                        (entry['service_id'], json.dumps(result)))

            # create client entry
            self._db_conn.insert_client(
                service_type, entry['service_id'], client_id, result, ttl)

            # update publisher TS for round-robin algorithm
            entry['ts_use'] = self._ts_use
            self._ts_use += 1
            self._db_conn.update_service(
                service_type, entry['service_id'], entry)


        response = {'ttl': ttl, service_type: r}
        if 'application/xml' in ctype:
            response = xmltodict.unparse({'response': response})
        return response
    # end api_subscribe

    # on-demand API to load-balance existing subscribers across all currently available
    # publishers. Needed if publisher gets added or taken down
    def api_lb_service(self, service_type):
        if service_type is None:
            bottle.abort(405, "Missing service")

        pubs = self._db_conn.lookup_service(service_type)
        if pubs is None:
            bottle.abort(405, 'Unknown service')
        pubs_active = [item for item in pubs if not self.service_expired(item)]

        # only load balance if over 5% deviation from average to avoid churn
        avg_per_pub = sum([entry['in_use'] for entry in pubs_active])/len(pubs_active)
        lb_list = {}
        for item in pubs_active:
            if item['in_use'] > int(1.05*avg_per_pub):
                lb_list[item['service_id']] = item['in_use'] - int(avg_per_pub)
        if len(lb_list) == 0:
            return

        clients = self._db_conn.get_all_clients(service_type=service_type)
        if clients is None:
            return

        self.syslog('Initial load-balance server-list: %s, avg-per-pub %d, clients %d' \
            % (lb_list, avg_per_pub, len(clients)))

        """
        Walk through all subscribers and mark one publisher per subscriber down
        for deletion later. We could have deleted subscription right here.
        However the discovery server view of subscribers will not match with actual
        subscribers till respective TTL expire. Note that we only mark down ONE
        publisher per subscriber to avoid much churn at the subscriber end.
            self._db_conn.delete_subscription(service_type, client_id, service_id)
        """
        clients_lb_done = []
        for client in clients:
            (service_type, client_id, service_id, mtime, ttl) = client
            if client_id not in clients_lb_done and service_id in lb_list and lb_list[service_id] > 0:
                self._db_conn.mark_delete_subscription(service_type, client_id, service_id)
                clients_lb_done.append(client_id)
                lb_list[service_id] -= 1
                self._debug['count_lb'] += 1
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
    # end api_subscribe

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

        for pub in self._db_conn.service_entries(service_type):
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

        for pub in self._db_conn.service_entries(service_type):
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

    def service_http_put(self, id):
        self.syslog('Update service %s' % (id))
        try:
            json_req = bottle.request.json
            service_type = json_req['service-type']
            self.syslog('Entry %s' % (json_req))
        except (ValueError, KeyError, TypeError) as e:
            bottle.abort(400, e)

        entry = self._db_conn.lookup_service(service_type, service_id=id)
        if not entry:
            bottle.abort(405, 'Unknown service')

        if 'admin-state' in json_req:
            admin_state = json_req['admin-state']
            if admin_state not in ['up', 'down']:
                bottle.abort(400, "Invalid admin state")
            entry['admin_state'] = admin_state
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
        for entry in self._db_conn.service_entries():
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
    }

    # per service options
    default_service_opts = {
        'policy': None,
    }
    service_config = {}

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        for section in config.sections():
            if section == "DEFAULTS":
                defaults.update(dict(config.items("DEFAULTS")))
                continue
            service_config[
                section.lower()] = default_service_opts.copy()
            service_config[section.lower()].update(
                dict(config.items(section)))

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
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec")

    args = parser.parse_args(remaining_argv)
    args.conf_file = args.conf_file
    args.service_config = service_config
    args.default_service_opts = default_service_opts
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
