#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This is the main module in discovery service package. It manages interaction
between http/rest and database interfaces.
"""

import gevent
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
from cassdb import DiscoveryCassendraClient

from disc_health import HealthCheck
from disc_utils  import *
import disc_consts 


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
#class LinkObject


class DiscoveryServer():

    def __init__(self, args_str = None):
        self._homepage_links = []
        self._args = None
        self._debug = {}
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
            self._base_url + '/publish', 'publish'))

        # subscribe service
        bottle.route('/subscribe',  'POST', self.api_subscribe)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/subscribe', 'subscribe'))

        # collection - services
        bottle.route('/services', 'GET', self.services_http_get)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/services', 'services'))

        # update service
        bottle.route('/service/<id>', 'PUT', self.service_http_put)

        # update service
        bottle.route('/service/<id>', 'GET', self.service_http_get)

        # collection - clients
        bottle.route('/clients', 'GET', self.clients_http_get)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/clients', 'clients'))

        # show config
        bottle.route('/config', 'GET', self.config_http_get)
        self._homepage_links.append(LinkObject('action',
            self._base_url + '/config', 'config'))

        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()

        # DB interface initialization
        self._db_connect(self._args.reset_config)

        # publish own service
    #end __init__

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
            json_links.append({'link': obj_to_json(link)})

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

                     --cassandra_server_list 10.1.2.3:9160
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
            'reset_config': False,
            'listen_ip_addr' : disc_consts._WEB_HOST,
            'listen_port' : disc_consts._WEB_PORT,
            'cassandra_server_list': disc_consts._CASSANDRA_HOST + ':' \
                                     + str(disc_consts._CASSANDRA_PORT),
            'ttl_min' : disc_consts._TTL_MIN,
            'ttl_max' : disc_consts._TTL_MAX,
            'hc_interval': disc_consts.HC_INTERVAL,
            'hc_max_miss': disc_consts.HC_MAX_MISS,
            }

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))

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

        parser.add_argument("--cassandra_server_list", nargs = '+',
                            help = "List of cassandra servers in IP Address:Port format")
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
        parser.add_argument("--hc_interval", type=int,
                            help = "Heartbeat interval, default %d seconds" %(disc_consts.HC_INTERVAL))
        parser.add_argument("--hc_max_miss", type=int,
                            help = "Maximum heartbeats to miss before declaring out-of-service, default %d" %(disc_consts.HC_MAX_MISS))
        self._args = parser.parse_args(remaining_argv)
        self._args.conf_file = args.conf_file
        self._args.cassandra_server_list = self._args.cassandra_server_list.split()
    #end _parse_args

    def _db_connect(self, reset_config):
        db_conn = DiscoveryCassendraClient(self._args.cassandra_server_list,
                                           reset_config)
        self._db_conn = db_conn
        self._db_conn.build_service_id_to_type_map()
    #end _db_connect

    def cleanup(self):
        pass
    #end cleanup

    # check if service expired (return color along)
    def service_expired(self, entry, include_color = False):
        #timedelta = datetime.datetime.utcnow() - entry['heartbeat']
        timedelta = datetime.timedelta(seconds = (int(time.time()) - entry['heartbeat']))
        if timedelta.seconds <= self._args.hc_interval:
            color = "#00FF00"   # green - all good
            expired = False
        elif timedelta.seconds > self._args.hc_interval*self._args.hc_max_miss:
            color = "#FF0000"   # red - publication expired
            expired = True
        else:
            color = "#FFFF00"   # yellow - missed some heartbeats
            expired = False

        if entry['admin_state'] != 'up':
            expired = True

        if include_color:
            return (expired, color, timedelta)
        else:
            return expired
    #end service_expired

    def heartbeat(self, sig):
        entry = self._db_conn.pub_id_to_service(sig)
        if not entry:
            self.debug['hb_stray'] += 1
            return

        entry['hbcount'] += 1
        entry['heartbeat'] = int(time.time())
        self._db_conn.insert_service(entry['service_type'], sig, entry)
        print 'heartbeat service "%s", sid=%s' %(entry['service_type'], sig)
    #end heartbeat

    def api_publish(self):
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
                'in_use':0, 
                'prov_state':'new', 
                'admin_state':'up', 
                'hbcount': 0,
                'remote': bottle.request.environ.get('REMOTE_ADDR'),
                'info':info
            }

        entry['heartbeat'] = int(time.time())
        self._db_conn.insert_service(service_type, sig, entry)

        response = {'cookie': sig}
        if ctype != 'application/json':
            response = xmltodict.unparse({'response':response})

        print 'publish service "%s", sid=%s, info=%s' \
            %(service_type, sig, info)

        return response
    #end api_publish

    def api_subscribe(self):
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
        count = int(json_req['instances'])

        r = []
        ttl = randint(self._args.ttl_min, self._args.ttl_max)

        # check existing subscriptions 
        subs = self._db_conn.lookup_subscription(service_type, client_id)
        print 'subscribe: service=%s, cid=%s, ttl=%d, cur=%d' \
            %(service_type, client_id, ttl, len(subs) if subs else 0)
        if subs:
            for service_id, result in subs:
                entry = self._db_conn.lookup_service(service_type, service_id = service_id)
                if self.service_expired(entry):
                    continue
                self._db_conn.insert_client(service_type, service_id, client_id, 
                    result, ttl + disc_consts.TTL_EXPIRY_DELTA)
                print ' refresh subscrition for server = %s' %(service_id)
                r.append(result)
                count -= 1
                if count == 0:
                    response = {'ttl': ttl, service_type: r}
                    if ctype == 'application/xml':
                        response = xmltodict.unparse({'response':response})
                    return response

        # lookup publishers of the service
        pubs = self._db_conn.lookup_service(service_type)
        if not pubs:
            return {'ttl': ttl, service_type: r}

        # eliminate inactive services
        pubs_active = [item for item in pubs if not self.service_expired(item)]
        print ' Found %s publishers, %d active' %(len(pubs), len(pubs_active))

        # find least loaded instances
        pubs = sorted(pubs_active, key=lambda service: service['in_use'])

        # prepare response
        for index in range(min(count, len(pubs))):
            entry = pubs[index]
            result = entry['info']

            print 'subscribe: service=%s, info=%s, sid=%s, cid=%s' \
                %(service_type, json.dumps(result), entry['service_id'], client_id)

            # create client entry
            self._db_conn.insert_client(service_type, entry['service_id'], client_id, 
                result, ttl + disc_consts.TTL_EXPIRY_DELTA)

            # update publisher entry 
            entry['in_use'] += 1
            self._db_conn.insert_service(service_type, entry['service_id'], entry)

            r.append(result)

        response = {'ttl': ttl, service_type: r}
        if ctype == 'application/xml':
            response = xmltodict.unparse({'response':response})
        return response
    #end api_subscribe

    def services_http_get(self):
        try:
            service_type = bottle.request.query['service']
        except (ValueError, KeyError) as e:
            service_type = None

        rsp = ''
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
            rsp += '    <tr>\n'
            rsp += '        <td>' + pub['service_type'] + '</td>\n'
            rsp += '        <td>' + pub['remote'] + '</td>\n'
            link = do_html_url("service/%s"%(pub['service_id']), pub['service_id'])
            rsp += '        <td>' + link + '</td>\n'
            rsp += '        <td>' + pub['prov_state'] + '</td>\n'
            rsp += '        <td>' + pub['admin_state'] + '</td>\n'
            rsp += '        <td>' + str(pub['in_use']) + '</td>\n'
            rsp += '        <td>' + str(pub['hbcount']) + '</td>\n'
            (expired, color, timedelta) = self.service_expired(pub, include_color = True)
            rsp += '        <td bgcolor=%s>' %(color) + str(timedelta) + '</td>\n'
            rsp += '    </tr>\n'
        rsp += ' </table>\n'

        return rsp
    #end show_services

    def service_http_put(self, id):
        try:
            json_req = bottle.request.json
            service_type = json_req['service_type']
        except (ValueError, KeyError, TypeError) as e:
            bottle.abort(400, e)

        entry = self._db_conn.lookup_service(service_type, service_id = id)
        if not entry: 
            bottle.abort(405, 'Unknown service')

        entry.update(json_req)
        self._db_conn.insert_service(service_type, id, entry)

        print 'update service=%s, sid=%s, info=%s' \
            %(service_type, id, entry)

        return {}
    #end service_http_put

    def service_http_get(self, id):
        entry = self._db_conn.pub_id_to_service(id)
        if entry:
            return entry['info']
        return {}
    #end service_http_get

    def clients_http_get(self):

        rsp = ''
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '    <tr>\n'
        rsp += '        <td>Service Type</td>\n'
        rsp += '        <td>Client Id</td>\n'
        rsp += '        <td>Service Id</td>\n'
        rsp += '    </tr>\n'

        # lookup subscribers of the service
        clients = self._db_conn.get_all_clients()

        if not clients:
            return rsp

        for client in clients:
            (service_type, client_id, service_id) = client
            rsp += '    <tr>\n'
            rsp += '        <td>' + service_type + '</td>\n'
            rsp += '        <td>' + client_id    + '</td>\n'
            link = do_html_url("service/%s"%(service_id), service_id)
            rsp += '        <td>' + link   + '</td>\n'
            rsp += '    </tr>\n'
        rsp += ' </table>\n'

        return rsp
    #end show_clients

    def config_http_get(self):
        return self._args.__dict__
    #end config_http_get

#end class DiscoveryServer

def main(args_str = None):
    server = DiscoveryServer(args_str)
    pipe_start_app = server.get_pipe_start_app()

    hc = HealthCheck(server.get_ip_addr(), server.get_port())
    hc.set_server_callback(server.heartbeat)
    gevent.spawn(hc.server)

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
