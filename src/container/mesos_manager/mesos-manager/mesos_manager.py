#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This is the main module in mesos-manager service package. It
manages interaction from CNI plugin, api-server http/REST API
and database interfaces.
"""

import gevent
from gevent import monkey
monkey.patch_all()

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
import mesos_consts

import bottle

from cfgm_common.rest import LinkObject
from cfgm_common import vnc_cgitb

def obj_to_json(obj):
    # Non-null fields in object get converted to json fields
    return lambda obj: dict((k, v) for k, v in obj.__dict__.iteritems())
# end obj_to_json

class MesosManager():

    def __init__(self, args):
        self._homepage_links = []
        self._args = args
        self.service_config = args.service_config
        self.cassandra_config = args.cassandra_config

        self._cni_data = {}

        self._base_url = "http://%s:%s" % (self._args.listen_ip_addr,
                                           self._args.listen_port)
        self._pipe_start_app = None
        bottle.route('/', 'GET', self.homepage_http_get)

        # Add CNI information
        bottle.route('/add_cni_info',  'POST', self.add_cni_info)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/add_cni_info', 'Add CNI information'))

        # Get CNI information
        bottle.route('/get_cni_info', 'GET', self.get_cni_info_all)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/get_cni_info', 'get all CNI information'))
        bottle.route('/get_cni_info.json', 'GET', self.get_cni_info_json)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/get_cni_info.json',
                'get CNI information in JSON format'))
        # get a specific CNI information
        bottle.route('/get_cni_info/<container_id>', 'GET', self.get_cni_info_all)

        # show config
        bottle.route('/config', 'GET', self.show_config)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/config', 'show cni config'))

        # show debug
        bottle.route('/stats', 'GET', self.show_stats)
        self._homepage_links.append(
            LinkObject(
                'action',
                self._base_url , '/stats', 'show cni debug stats'))
        bottle.route('/stats.json', 'GET', self.show_stats_json)

        # cleanup
        bottle.route('/cleanup', 'GET', self.cleanup_http_get)
        self._homepage_links.append(LinkObject('action',
            self._base_url , '/cleanup', 'Purge deleted cni'))

        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()

    # end __init__

    Store CNI Information
    def create_cni_data(self, container_id, data):
        if not container_id in self._cni_data:
            self._cni_data[container_id] = {}
            self._cni_data[container_id] = data
        return self._cni_data[container_id]
    # end

    def delete_cni_data(self, container_id):
        if container_id in self._cni_data:
            del self._cni_data[container_id]
    # end

    def get_cni_data(self, container_id, service_type):
        if container_id in self._cni_data:
            return self._cni_data[container_id]
        return None
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

    def add_cni_info(self):
        return "Add CNI Info"
    #end

    def get_cni_info_all(self):
        return "GEt CNI Info All"
    #end

    def get_cni_info_json(self):
        return "Get Net Info JSON "
    #end

    # purge expired cni
    def cleanup_http_get(self):
        return "Cleanup"
    #end

    def show_config(self):
        rsp = ""

        rsp += '<table border="1" cellpadding="1" cellspacing="0">\n'
        rsp += '<tr><th colspan="2">Defaults CONFIGG</th></tr>'
        for k in sorted(self._args.__dict__.iterkeys()):
            v = self._args.__dict__[k]
            rsp += '<tr><td>%s</td><td>%s</td></tr>' % (k, v)
        rsp += '</table>'
        rsp += '<br>'

        return rsp
    # end show_config

    def show_stats(self):

        rsp = ""
        rsp += ' <table border="1" cellpadding="1" cellspacing="0">\n'
        for k in sorted(stats.iterkeys()):
            rsp += '    <tr>\n'
            rsp += '        <td>%s</td>\n' % (k)
            rsp += '        <td>STATSSS</td>\n'
            rsp += '    </tr>\n'
        return rsp
    # end show_stats

    def show_stats_json(self):
        stats = "JSONNN"
        return stats
    # end show_stats_json

# end class DiscoveryServer

def parse_args(args_str):
    '''
    Eg. python mesos_mgr.py
            --cassandra_server_ip 127.0.0.1
            --cassandra_server_port 9160
            --listen_ip_addr 127.0.0.1
            --listen_port 6991
    '''

    # Print all options in response to -h
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
        'listen_ip_addr': mesos_consts._WEB_HOST,
        'listen_port': mesos_consts._WEB_PORT,
        'cassandra_server_ip': mesos_consts._CASSANDRA_HOST,
        'cassandra_server_port': mesos_consts._CASSANDRA_PORT,
        'cassandra_max_retries': mesos_consts._CASSANDRA_MAX_RETRIES,
        'cassandra_timeout': mesos_consts._CASSANDRA_TIMEOUT,
        'use_syslog': False,
        'log_category': '',
    }

    cassandra_opts = {
        'cassandra_user'     : None,
        'cassandra_password' : None,
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
            if section == "DEFAULT":
                defaults.update(dict(config.items("DEFAULT")))
                continue
            if section == "MESOS":
                defaults.update(dict(config.items("MESOS")))
                continue

    parser.set_defaults(**defaults)

    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--cassandra_max_retries", type=int,
        help="Maximum number of request retry, default %d"
        % (mesos_consts._CASSANDRA_MAX_RETRIES))
    parser.add_argument(
        "--cassandra_timeout", type=float,
        help="Timeout of request, default %d"
        % (mesos_consts._CASSANDRA_TIMEOUT))
    parser.add_argument(
        "--listen_ip_addr",
        help="IP address to provide service on, default %s"
        % (mesos_consts._WEB_HOST))
    parser.add_argument(
        "--listen_port", type=int,
        help="Port to provide service on, default %s"
        % (mesos_consts._WEB_PORT))
    parser.add_argument("--cassandra_user",
            help="Cassandra user name")
    parser.add_argument("--cassandra_password",
            help="Cassandra password")

    args = parser.parse_args(remaining_argv)
    args.conf_file = args.conf_file
    args.service_config = service_config
    args.cassandra_config = cassandra_config
    args.cassandra_opts = cassandra_opts
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list =\
            args.cassandra_server_list.split()
    return args
# end parse_args

def run_mesos_manager(args):
    server = MesosManager(args)
    pipe_start_app = server.get_pipe_start_app()

    try:
        bottle.run(app=pipe_start_app, host=server.get_ip_addr(),
                   port=server.get_port(), server='gevent')
    except Exception as e:
        # cleanup gracefully
        server.cleanup()
#end run_mesos_manager

def main(args_str=None):
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    run_mesos_manager(args)
# end main

def mesos_manager_main():
    vnc_cgitb.enable(format='text')
    main()
# end mesos_manager_main

if __name__ == "__main__":
    mesos_manager_main()
