#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This is the main module in discovery service package. It manages interaction
between http/rest and database interfaces.
"""

import gevent
from disc_zk import DiscoveryZkClient
from gevent import monkey; monkey.patch_all()
from gevent import hub

import sys
import time
import logging
import signal
import os
import socket
import json, xmltodict
import uuid
import copy
import datetime
import argparse
import ConfigParser
from pprint import pformat
from random import randint

import bottle

from disc_utils  import *
import disc_consts 
import output

# sandesh
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.vns.ttypes import Module
from sandesh.vns.constants import ModuleNames
from sandesh.discovery_introspect import ttypes as sandesh

from gevent.coros import BoundedSemaphore

def obj_to_json(obj):
    # Non-null fields in object get converted to json fields
    return lambda obj: dict((k, v) for k, v in obj.__dict__.iteritems())
#end obj_to_json

class LinkObject(object):
    def __init__(self, rel, href, name):
        self.rel = rel
        self.href = href
        self.name = name
    #end __init__

    def to_dict(self):
        return {'rel': self.rel,
                'href': self.href,
                'name': self.name}
#class LinkObject


class DiscoveryServer():

    def __init__(self, args_str = None):
        self._homepage_links = []
        self._args = None
        self._debug = {
            'hb_stray':0, 
            'msg_pubs':0, 
            'msg_subs':0, 
            'msg_query':0, 
            'heartbeats':0, 
            'ttl_short':0,
            'policy_rr':0,
            'policy_lb':0,
            'policy_fi':0,
        }
        self._ts_use = 1
        self.short_ttl_map = {}
        self._sem = BoundedSemaphore(1)
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._base_url = "http://%s:%s" %(self._args.listen_ip_addr,
                                          self._args.listen_port)
        self._pipe_start_app = None

        bottle.route('/', 'GET', self.homepage_http_get)

        # publish service
        bottle.route('/publish', 'POST', self.api_publish)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/publish', 'publish service'))

        # subscribe service
        bottle.route('/subscribe',  'POST', self.api_subscribe)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/subscribe', 'subscribe service'))

        # query service
        bottle.route('/query',  'POST', self.api_query)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/query', 'query service'))

        # collection - services
        bottle.route('/services', 'GET', self.show_all_services)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/services', 'show published services'))
        bottle.route('/services.json', 'GET', self.services_json)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/services.json', 'List published services in JSON format'))
        # show a specific service type
        bottle.route('/services/<service_type>', 'GET', self.show_all_services)

        # update service
        bottle.route('/service/<id>', 'PUT', self.service_http_put)

        # get service info
        bottle.route('/service/<id>', 'GET',  self.service_http_get)
        bottle.route('/service/<id>/brief', 'GET', self.service_brief_http_get)

        # delete (un-publish) service
        bottle.route('/service/<id>', 'DELETE', self.service_http_delete)

        # collection - clients
        bottle.route('/clients', 'GET', self.show_all_clients)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/clients', 'list all subscribers'))
        bottle.route('/clients.json', 'GET', self.clients_json)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/clients.json', 'list all subscribers in JSON format'))

        # show config
        bottle.route('/config', 'GET', self.config_http_get)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/config', 'show discovery service config'))

        # show debug
        bottle.route('/stats', 'GET', self.show_stats)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/stats', 'show discovery service stats'))

        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()

        # sandesh init
        collectors = None
        if self._args.collector and self._args.collector_port:
            collectors = [(self._args.collector, int(self._args.collector_port))]
        self._sandesh = Sandesh()
        self._sandesh.init_generator(ModuleNames[Module.DISCOVERY_SERVICE], 
                socket.gethostname(), collectors, 'discovery_context', 
                int(self._args.http_server_port), ['sandesh', 'uve'])
        self._sandesh.set_logging_params(enable_local_log = self._args.log_local,
                                         category = self._args.log_category,
                                         level = self._args.log_level,
                                         file = self._args.log_file)
        self._sandesh.trace_buffer_create(name = "dsHeartBeatTraceBuf", size = 1000)

        # DB interface initialization
        self._db_connect(self._args.reset_config)

        # build in-memory publisher data
        self._pub_data = {}
        for entry in self._db_conn.service_entries():
            self.create_pub_data(entry['service_id'], entry['service_type'])

        # build in-memory subscriber data
        self._sub_data = {}
        for (client_id, service_type) in self._db_conn.subscriber_entries():
            self.create_sub_data(client_id, service_type)

        # must be done after we have built in-memory publisher data from db.
        self._db_conn.start_background_tasks()
    #end __init__

    def create_pub_data(self, service_id, service_type):
        self._pub_data[service_id] = {
            'service_type': service_type,
            'hbcount'     : 0,
            'heartbeat'   : int(time.time()),
        }

    def create_sub_data(self, client_id, service_type):
        if not client_id in self._sub_data:
            self._sub_data[client_id] = {}
        if not service_type in self._sub_data[client_id]:
            sdata = {
                'ttl_expires' : 0,
                'heartbeat'   : int(time.time()),
            }
            self._sub_data[client_id][service_type] = sdata
        return self._sub_data[client_id][service_type]
    #end

    def delete_sub_data(self, client_id, service_type):
        if client_id in self._sub_data and service_type in self._sub_data[client_id]:
            del self._sub_data[client_id][service_type]
            if len(self._sub_data[client_id]) == 0:
                del self._sub_data[client_id]
    #end

    def get_pub_data(self, id):
        return self._pub_data.get(id, None)
    #end 

    def get_sub_data(self, id, service_type):
        if id in self._sub_data:
            return self._sub_data[id].get(service_type, None)
        return None
    #end 

    # Public Methods
    def get_args(self):
        return self._args
    #end get_args

    def get_ip_addr(self):
        return self._args.listen_ip_addr
    #end get_ip_addr

    def get_port(self):
        return self._args.listen_port
    #end get_port

    def get_pipe_start_app(self):
        return self._pipe_start_app
    #end get_pipe_start_app

    def homepage_http_get(self):
        json_links = []
        for link in self._homepage_links:
            json_links.append({'link': link.to_dict()})

        json_body = \
            { "href": self._base_url,
              "links": json_links
            }

        return json_body
    #end homepage_http_get

    # Private Methods
    def _parse_args(self, args_str):
        '''
        Eg. python discovery.py 

                     --zk_server_ip 10.1.2.3
                     --zk_server_port 9160
                     --listen_ip_addr 127.0.0.1
                     --listen_port 5998
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'reset_config'     : False,
            'listen_ip_addr'   : disc_consts._WEB_HOST,
            'listen_port'      : disc_consts._WEB_PORT,
            'zk_server_ip'     : disc_consts._ZK_HOST,
            'zk_server_port'   : disc_consts._ZK_PORT,
            'ttl_min'          : disc_consts._TTL_MIN,
            'ttl_max'          : disc_consts._TTL_MAX,
            'ttl_short'        : 0,
            'hc_interval'      : disc_consts.HC_INTERVAL,
            'hc_max_miss'      : disc_consts.HC_MAX_MISS,
            'collector'        : '127.0.0.1',
            'collector_port'   : '8086',
            'http_server_port' : '5997',
            'log_local'        : False,
            'log_level'        : SandeshLevel.SYS_DEBUG,
            'log_category'     : '',
            'log_file'         : Sandesh._DEFAULT_LOG_FILE
            }

        # per service options
        self.default_service_opts = {
            'policy': None,
        }
        self.service_config = {}

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
            for section in config.sections():
                if section == "DEFAULTS":
                    continue
                self.service_config[section.lower()] = self.default_service_opts
                self.service_config[section.lower()].update(dict(config.items(section)))

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
        parser.set_defaults(**defaults)

        parser.add_argument("--zk_server_ip",
                            help = "IP address of zk server")
        parser.add_argument("--zk_server_port", type=int,
                            help = "Port of zk server")
        parser.add_argument("--reset_config", action = "store_true",
                            help = "Warning! Destroy previous configuration and start clean")
        parser.add_argument("--listen_ip_addr",
                            help = "IP address to provide service on, default %s" %(disc_consts._WEB_HOST))
        parser.add_argument("--listen_port", type=int,
                            help = "Port to provide service on, default %s" %(disc_consts._WEB_PORT))
        parser.add_argument("--ttl_min", type=int,
                            help = "Minimum time to cache service information, default %d" %(disc_consts._TTL_MIN))
        parser.add_argument("--ttl_max", type=int,
                            help = "Maximum time to cache service information, default %d" %(disc_consts._TTL_MAX))
        parser.add_argument("--ttl_short", type=int,
                            help = "Short TTL for agressively subscription schedule")
        parser.add_argument("--hc_interval", type=int,
                            help = "Heartbeat interval, default %d seconds" %(disc_consts.HC_INTERVAL))
        parser.add_argument("--hc_max_miss", type=int,
                            help = "Maximum heartbeats to miss before declaring out-of-service, default %d" %(disc_consts.HC_MAX_MISS))
        parser.add_argument("--collector",
                            help = "IP address of VNC collector server")
        parser.add_argument("--collector_port",
                            help = "Port of VNC collector server")
        parser.add_argument("--http_server_port",
                            help = "Port of local HTTP server")
        parser.add_argument("--log_local", action = "store_true",
                            help = "Enable local logging of sandesh messages")
        parser.add_argument("--log_level",
                            help = "Severity level for local logging of sandesh messages")
        parser.add_argument("--log_category",
                            help = "Category filter for local logging of sandesh messages")
        parser.add_argument("--log_file",
                            help = "Filename for the logs to be written to")
        self._args = parser.parse_args(remaining_argv)
        self._args.conf_file = args.conf_file

    #end _parse_args

    def get_service_config(self, service_type, item):
        service = service_type.lower()
        if service in self.service_config and item in self.service_config[service]:
            return self.service_config[service][item]
        elif item in self._args.__dict__:    
            return self._args.__dict__[item]
        else:
            return None
    #end

    def _db_connect(self, reset_config):
        zk_ip = self._args.zk_server_ip
        zk_port = self._args.zk_server_port

        self._db_conn = DiscoveryZkClient(self, zk_ip, zk_port, reset_config)
    #end _db_connect

    def cleanup(self):
        pass
    #end cleanup

    def syslog(self, log_msg):
        log = sandesh.discServiceLog(log_msg = log_msg, sandesh = self._sandesh)
        log.send(sandesh = self._sandesh)

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
    #end

    # check if service expired (return color along)
    def service_expired(self, entry, include_color = False, include_down = True):
        pdata = self.get_pub_data(entry['service_id'])
        timedelta = datetime.timedelta(seconds = (int(time.time()) - pdata['heartbeat']))
        if timedelta.seconds <= self._args.hc_interval:
            color = "#00FF00"   # green - all good
            expired = False
        elif timedelta.seconds > self._args.hc_interval*self._args.hc_max_miss:
            color = "#FF0000"   # red - publication expired
            expired = True
        else:
            color = "#FFFF00"   # yellow - missed some heartbeats
            expired = False

        if include_down and entry['admin_state'] != 'up':
            color = "#FF0000"   # red - publication expired
            expired = True

        if include_color:
            return (expired, color, timedelta)
        else:
            return expired
    #end service_expired

    def heartbeat(self, sig):
        self._debug['heartbeats'] += 1
        pdata = self.get_pub_data(sig)
        if not pdata:
            self.syslog('Received stray hearbeat with cookie %s' %(sig))
            self._debug['hb_stray'] += 1
            # resource not found
            return '404 Not Found'

        pdata['hbcount'] += 1
        pdata['heartbeat'] = int(time.time())

        m = sandesh.dsHeartBeat(publisher_id=sig, service_type=pdata['service_type'], sandesh=self._sandesh)
        m.trace_msg(name='dsHeartBeatTraceBuf', sandesh=self._sandesh)
        return '200 OK'
        #print 'heartbeat service "%s", sid=%s' %(entry['service_type'], sig)
    #end heartbeat

    def handle_heartbeat(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind((self.get_ip_addr(), self.get_port()))
        while True:
            data, addr = sock.recvfrom(1024)
            """
            print ''
            print 'addr = ', addr
            print 'data = ', data
            """
            data = xmltodict.parse(data)
            status = self.heartbeat(data['cookie'])

            # send status back to publisher
            sock.sendto(status, addr)
    #end start

    def api_publish(self):
        self._debug['msg_pubs'] += 1
        ctype = bottle.request.headers['content-type']
        json_req = {}
        if ctype == 'application/json':
            data = bottle.request.json
            for service_type, info in data.items():
                json_req['name'] = service_type
                json_req['info'] = info
        elif ctype == 'application/xml':
            data = xmltodict.parse(bottle.request.body.read())
            for service_type, info in data.items():
                json_req['name'] = service_type
                json_req['info'] = dict(info)
        else:
            bottle.abort(400, e)

        sig = publisher_id(bottle.request.environ['REMOTE_ADDR'], json.dumps(json_req))

        # Rx {'name': u'ifmap-server', 'info': {u'ip_addr': u'10.84.7.1', u'port': u'8443'}}
        info = json_req['info']
        service_type = json_req['name']

        entry = self._db_conn.lookup_service(service_type, service_id = sig)
        if not entry:
            entry = {
                'service_type': service_type,
                'service_id':sig, 
                'in_use'    :0, 
                'ts_use'    :0, 
                'ts_created': int(time.time()),
                'prov_state':'new', 
                'remote'    : bottle.request.environ.get('REMOTE_ADDR'),
                'info'      :info,
            }
            self.create_pub_data(sig, service_type)

        entry['admin_state'] = 'up'
        self._db_conn.insert_service(service_type, sig, entry)

        response = {'cookie': sig}
        if ctype != 'application/json':
            response = xmltodict.unparse({'response':response})

        self.syslog('publish service "%s", sid=%s, info=%s' \
            %(service_type, sig, info))

        if not service_type.lower() in self.service_config:
            self.service_config[service_type.lower()] = self.default_service_opts

        return response
    #end api_publish

    # find least loaded service instances - sort by subscriber count
    def service_list_round_robin(self, pubs):
        self._debug['policy_rr'] += 1
        return sorted(pubs, key=lambda service: service['in_use'])
    #end

    # most recently used on top of round robin - MRU first
    def service_list_load_balance(self, pubs):
        self._debug['policy_lb'] += 1
        temp = sorted(pubs, key=lambda service: service['ts_use'], reverse=True)
        return sorted(temp, key=lambda service: service['in_use'])
    #end

    # master election
    def service_list_fixed(self, pubs):
        self._debug['policy_fi'] += 1
        return sorted(pubs, key=lambda service: service['sequence'])
    #end

    def service_list(self, service_type, pubs):
        policy = self.get_service_config(service_type, 'policy')

        if policy == 'load-balance':
            f = self.service_list_load_balance
        elif policy == 'fixed':
            f = self.service_list_fixed
        else:
            f = self.service_list_round_robin

        return f(pubs)
    #end


    def api_subscribe(self):
        self._debug['msg_subs'] += 1
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
        client_id = json_req['client']
        count = reqcnt = int(json_req['instances'])
        client_type = json_req.get('client-type', '')

        assigned_sid = set()
        r = []
        ttl = randint(self._args.ttl_min, self._args.ttl_max)

        cl_entry = self._db_conn.lookup_client(service_type, client_id)
        if not cl_entry:
            cl_entry = {
                'instances': count,
                'remote': bottle.request.environ.get('REMOTE_ADDR'),
                'client_type': client_type,
            }
            self.create_sub_data(client_id, service_type)
            self._db_conn.insert_client_data(service_type, client_id, cl_entry)
            self.syslog('subscribe: service type=%s, client=%s:%s, ttl=%d, asked=%d' \
                %(service_type, client_type, client_id, ttl, count))

        sdata = self.get_sub_data(client_id, service_type)
        sdata['ttl_expires'] += 1

        # check existing subscriptions 
        subs = self._db_conn.lookup_subscription(service_type, client_id)
        if subs:
            for service_id, result in subs:
                entry = self._db_conn.lookup_service(service_type, service_id = service_id)
                if self.service_expired(entry):
                    #self.syslog('skipping expired service %s, info %s' %(service_id, entry['info']))
                    continue
                self._db_conn.insert_client(service_type, service_id, client_id, result, ttl)
                #self.syslog(' refresh subscrition for service %s' %(service_id))
                r.append(result)
                assigned_sid.add(service_id)
                count -= 1
                if count == 0:
                    response = {'ttl': ttl, service_type: r}
                    if ctype == 'application/xml':
                        response = xmltodict.unparse({'response':response})
                    return response

        # acquire lock to update use count and TS
        self._sem.acquire()

        # lookup publishers of the service
        pubs = self._db_conn.lookup_service(service_type)
        if not pubs:
            # force client to come back soon if service expectation is not met
            if  len(r) < reqcnt:
                ttl_short = self.get_service_config(service_type, 'ttl_short')
                if ttl_short:
                    ttl = self.get_ttl_short(client_id, service_type, ttl_short)
                    self._debug['ttl_short'] += 1
                    #self.syslog(' sending short ttl %d to %s' %(ttl, client_id))

            response = {'ttl': ttl, service_type: r}
            if ctype == 'application/xml':
                response = xmltodict.unparse({'response':response})
            self._sem.release()
            return response

        # eliminate inactive services
        pubs_active = [item for item in pubs if not self.service_expired(item)]
        #self.syslog(' Found %s publishers, %d active, need %d' %(len(pubs), len(pubs_active), count))

        # find least loaded instances
        pubs = self.service_list(service_type, pubs_active)

        # prepare response - send all if count 0
        for index in range(min(count, len(pubs)) if count else len(pubs)):
            entry = pubs[index]

            # skip duplicates - could happen if some publishers have quit and
            # we have already picked up others from cached information above
            if entry['service_id'] in assigned_sid:
                continue
            assigned_sid.add(entry['service_id'])

            result = entry['info']
            r.append(result)

            self.syslog(' assign service=%s, info=%s' %(entry['service_id'], json.dumps(result)))

            # don't update pubsub data if we are sending entire list
            if count == 0:
                continue

            # create client entry
            self._db_conn.insert_client(service_type, entry['service_id'], client_id, result, ttl)

            # update publisher entry 
            entry['in_use'] += 1
            entry['ts_use'] = self._ts_use; self._ts_use += 1
            self._db_conn.update_service(service_type, entry['service_id'], entry)

        self._sem.release()

        # force client to come back soon if service expectation is not met
        if  len(r) < reqcnt:
            ttl_short = self.get_service_config(service_type, 'ttl_short')
            if ttl_short:
                ttl = self.get_ttl_short(client_id, service_type, ttl_short)
                self._debug['ttl_short'] += 1
                #self.syslog(' sending short ttl %d to %s' %(ttl, client_id))

        response = {'ttl': ttl, service_type: r}
        if ctype == 'application/xml':
            response = xmltodict.unparse({'response':response})
        return response
    #end api_subscribe

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
        self.syslog(' query: Found %s publishers, %d active, need %d' %(len(pubs), len(pubs_active), count))

        # find least loaded instances
        pubs = pubs_active

        # prepare response - send all if count 0
        for index in range(min(count, len(pubs)) if count else len(pubs)):
            entry = pubs[index]

            result = entry['info']
            r.append(result)

            self.syslog(' assign service=%s, info=%s' %(entry['service_id'], json.dumps(result)))

            # don't update pubsub data if we are sending entire list
            if count == 0:
                continue

        response = {service_type: r}
        if ctype == 'application/xml':
            response = xmltodict.unparse({'response':response})
        return response
    #end api_subscribe

    def show_all_services(self, service_type = None):

        rsp = output.display_user_menu()
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Service Type</td>\n'
        rsp += '        <td>Remote IP</td>\n'
        rsp += '        <td>Service Id</td>\n'
        rsp += '        <td>Provision State</td>\n'
        rsp += '        <td>Admin State</td>\n'
        rsp += '        <td>In Use</td>\n'
        rsp += '        <td>Heartbeats</td>\n'
        rsp += '        <td>Time since last Heartbeat</td>\n'
        rsp += '    </tr>\n'

        # lookup publishers of the service
        if service_type:
            pubs = self._db_conn.lookup_service(service_type)
        else:
            pubs = self._db_conn.get_all_services()

        if not pubs:
            return rsp

        for pub in pubs:
            info = pub['info']
            pdata = self.get_pub_data(pub['service_id'])
            rsp += '    <tr>\n'
            if service_type:
                rsp += '        <td>' + pub['service_type'] + '</td>\n'
            else:
                link = do_html_url("/services/%s"%(pub['service_type']), pub['service_type'])
                rsp += '        <td>' + link + '</td>\n'
            rsp += '        <td>' + pub['remote'] + '</td>\n'
            link = do_html_url("/service/%s/brief"%(pub['service_id']), pub['service_id'])
            rsp += '        <td>' + link + '</td>\n'
            rsp += '        <td>' + pub['prov_state'] + '</td>\n'
            rsp += '        <td>' + pub['admin_state'] + '</td>\n'
            rsp += '        <td>' + str(pub['in_use']) + '</td>\n'
            rsp += '        <td>' + str(pdata['hbcount']) + '</td>\n'
            (expired, color, timedelta) = self.service_expired(pub, include_color = True)
            #status = "down" if expired else "up"
            rsp += '        <td bgcolor=%s>' %(color) + str(timedelta) + '</td>\n'
            rsp += '    </tr>\n'
        rsp += ' </table>\n'

        return rsp
    #end show_services

    def services_json(self, service_type = None):
        rsp = []

        # lookup publishers of the service
        if service_type:
            pubs = self._db_conn.lookup_service(service_type)
        else:
            pubs = self._db_conn.get_all_services()

        if not pubs:
            return {'services': rsp}

        for pub in pubs:
            entry = pub.copy()
            pdata = self.get_pub_data(pub['service_id'])
            entry['hbcount'] = pdata['hbcount']
            entry['status'] = "down" if self.service_expired(entry) else "up"
            entry['heartbeat'] = pdata['heartbeat']
            rsp.append(entry)
        return {'services': rsp}
    #end services_json

    def service_http_put(self, id):
        self.syslog('Update service %s' %(id))
        try:
            json_req = bottle.request.json
            service_type = json_req['service_type']
            self.syslog('Entry %s' %(json_req))
        except (ValueError, KeyError, TypeError) as e:
            bottle.abort(400, e)

        entry = self._db_conn.lookup_service(service_type, service_id = id)
        if not entry: 
            bottle.abort(405, 'Unknown service')

        if 'admin_state' in json_req:
            entry['admin_state'] = json_req['admin_state']
        self._db_conn.update_service(service_type, id, entry)

        self.syslog('update service=%s, sid=%s, info=%s' \
            %(service_type, id, entry))

        return {}
    #end service_http_put

    def service_http_delete(self, id):
        self.syslog('Delete service %s' %(id))
        pdata = self.get_pub_data(id)
        entry = self._db_conn.lookup_service(pdata['service_type'], id)
        if not entry: 
            bottle.abort(405, 'Unknown service')
        service_type = entry['service_type']

        entry['admin_state'] = 'down'
        self._db_conn.update_service(service_type, id, entry)

        self.syslog('delete service=%s, sid=%s, info=%s' \
            %(service_type, id, entry))

        return {}
    #end service_http_put

    # return service info - meta as well as published data
    def service_http_get(self, id):
        entry = {}
        pdata = self.get_pub_data(id)
        pub = self._db_conn.lookup_service(pdata['service_type'], id)
        if pub:
            entry = pub.copy()
            entry['hbcount'] = pdata['hbcount']
            entry['status'] = "down" if self.service_expired(entry) else "up"
            entry['heartbeat'] = pdata['heartbeat']

        return entry
    #end service_http_get

    # return service info - only published data
    def service_brief_http_get(self, id):
        pdata = self.get_pub_data(id)
        entry = self._db_conn.lookup_service(pdata['service_type'], id)
        return entry['info']
    #end service_http_get

    def show_all_clients(self):

        rsp = output.display_user_menu()
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Client IP</td>\n'
        rsp += '        <td>Client Type</td>\n'
        rsp += '        <td>Client Id</td>\n'
        rsp += '        <td>Service Type</td>\n'
        rsp += '        <td>Service Id</td>\n'
        rsp += '        <td>TTL (sec)</td>\n'
        rsp += '        <td>TTL Refreshes</td>\n'
        rsp += '        <td>Last refreshed</td>\n'
        rsp += '    </tr>\n'

        # lookup subscribers of the service
        clients = self._db_conn.get_all_clients()

        if not clients:
            return rsp

        for client in clients:
            (service_type, client_id, service_id, mtime, ttl) = client
            cl_entry = self._db_conn.lookup_client(service_type, client_id)
            if cl_entry is None:
                continue
            sdata = self.get_sub_data(client_id, service_type)
            if sdata is None:
                self.syslog('Missing sdata for client %s, service %s' %(client_id, service_type))
                continue
            rsp += '    <tr>\n'
            rsp += '        <td>' + cl_entry['remote'] + '</td>\n'
            client_type = cl_entry.get('client_type', '')
            rsp += '        <td>' + client_type  + '</td>\n'
            rsp += '        <td>' + client_id    + '</td>\n'
            rsp += '        <td>' + service_type + '</td>\n'
            link = do_html_url("service/%s/brief"%(service_id), service_id)
            rsp += '        <td>' + link   + '</td>\n'
            rsp += '        <td>' + str(ttl) + '</td>\n'
            rsp += '        <td>' + str(sdata['ttl_expires']) + '</td>\n'
            rsp += '        <td>' + time.ctime(mtime) + '</td>\n'
            rsp += '    </tr>\n'
        rsp += ' </table>\n'

        return rsp
    #end show_clients

    def clients_json(self):

        rsp = []
        clients = self._db_conn.get_all_clients()

        if not clients:
            return {'services': rsp}

        for client in clients:
            (service_type, client_id, service_id, mtime, ttl) = client
            cl_entry = self._db_conn.lookup_client(service_type, client_id)
            sdata = self.get_sub_data(client_id, service_type)
            entry = cl_entry.copy()
            entry.update(sdata)

            entry['client_id'] = client_id
            entry['service_type'] = service_type
            entry['service_id'] = service_id
            entry['ttl'] = ttl
            rsp.append(entry)

        return {'services': rsp}
    #end show_clients

    def config_http_get(self):
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
        for k, v in self._args.__dict__.items():
            rsp += '<tr><td>%s</td><td>%s</td></tr>' %(k, v)
        rsp += '</table>'
        rsp += '<br>'

        for service, config in self.service_config.items():
            #rsp += '<h4>%s:</h4>' %(service)
            rsp += '<table border="1" cellpadding="1" cellspacing="0">\n'
            rsp += '<tr><th colspan="2">%s</th></tr>' %(service)
            for k, v in config.items():
                rsp += '<tr><td>%s</td><td>%s</td></tr>' %(k, v)
            rsp += '</table>'
            rsp += '<br>'
        return rsp
    #end config_http_get

    def show_stats(self):
        stats = self._debug
        stats.update(self._db_conn.get_debug_stats())

        rsp = output.display_user_menu()
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Publishers</td>\n'
        rsp += '        <td>%s</td>\n' % len(self._pub_data)
        rsp += '    </tr>\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Subscribers</td>\n'
        rsp += '        <td>%s</td>\n' % len(self._sub_data)
        rsp += '    </tr>\n'
        for k, v in stats.items():
            rsp += '    <tr>\n'
            rsp += '        <td>%s</td>\n' %(k)
            rsp += '        <td>%s</td>\n' %(v)
            rsp += '    </tr>\n'
        return rsp
    #end config_http_get

#end class DiscoveryServer

def main(args_str = None):
    server = DiscoveryServer(args_str)
    pipe_start_app = server.get_pipe_start_app()

    gevent.spawn(server.handle_heartbeat)

    try:
        bottle.run(app = pipe_start_app, host = server.get_ip_addr(), 
            port = server.get_port(), server = 'gevent')
    except Exception as e:
        # cleanup gracefully
        server.cleanup()

#end main

if __name__ == "__main__":
    import cgitb
    cgitb.enable(format='text')

    main()
