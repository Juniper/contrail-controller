#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import ConfigParser

#from ginkgo import Service
#from fabric.api import env
#from fabric.api import run
#from fabric.context_managers import settings

import eventlet
import os
import sys
#eventlet.monkey_patch(thread=False)

import uuid
import time
import errno
import socket
import subprocess

from vnc_api.vnc_api import *

import json
sys.path.insert(2, '/opt/stack/python-quantumclient')
from pprint import pformat
from quantumclient.quantum import client
from quantumclient.client import HTTPClient
from quantumclient.common import exceptions

class DemoCfg(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        httpclient = HTTPClient(username='admin',
                                tenant_name='demo',
                                password='contrail123',
                                #region_name=self._region_name,
                                auth_url='http://%s:5000/v2.0' %(self._args.api_server_ip))
        httpclient.authenticate()
        
        #OS_URL = httpclient.endpoint_url
        OS_URL = 'http://%s:9696/' %(self._args.api_server_ip)
        OS_TOKEN = httpclient.auth_token
        self._quantum = client.Client('2.0', endpoint_url=OS_URL, token = OS_TOKEN)

        self._vnc_lib = VncApi(self._args.admin_user,
                               self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip,
                               self._args.api_server_port, '/')

        self._create_vn('public', self._args.public_subnet)
        self._policy_link_vns()
    #end __init__

    def _create_vn(self, vn_name, vn_subnet):
        print "Creating network %s, subnet %s" %(vn_name, vn_subnet)
        net_req = {'name': '%s' %(vn_name)}
        net_rsp = self._quantum.create_network({'network': net_req})
        net1_id = net_rsp['network']['id']
        net1_fq_name = net_rsp['network']['contrail:fq_name']
        net1_fq_name_str = ':'.join(net1_fq_name)
        self._create_subnet(unicode(vn_subnet), net1_id)
    #end _create_vn

    def _policy_link_vns(self):
        net1_id, net2_id, net1_fq_name, net2_fq_name = \
            self._create_two_vns(vn1_name = 'front-end', vn2_name = 'back-end')

        net1_fq_name_str = ':'.join(net1_fq_name)
        net2_fq_name_str = ':'.join(net2_fq_name)

        print "Creating policy front-end-to-back-end"
        np_rules = [PolicyRuleType(direction = '<>', simple_action = 'pass', protocol = 'any',
                        src_addresses = [AddressType(virtual_network = net1_fq_name_str)],
                        src_ports = [PortType(-1, -1)],
                        dst_addresses = [AddressType(virtual_network = net2_fq_name_str)],
                        dst_ports = [PortType(-1, -1)])]
        pol_entries = PolicyEntriesType(np_rules)
        pol_entries_dict = \
            json.loads(json.dumps(pol_entries,
                            default=lambda o: dict((k,v) for k, v in o.__dict__.iteritems())))
        policy_req = {'name': 'front-end-to-back-end',
                      'entries': pol_entries_dict}
        
        policy_rsp = self._quantum.create_policy({'policy': policy_req})
        policy1_fq_name = policy_rsp['policy']['fq_name']
        
        print "Setting front-end policy to [front-end-to-back-end]"
        net_req = {'contrail:policys': [policy1_fq_name]}
        net_rsp = self._quantum.update_network(net1_id, {'network': net_req})
        
        print "Setting back-end policy to [front-end-to-back-end]"
        net_req = {'contrail:policys': [policy1_fq_name]}
        net_rsp = self._quantum.update_network(net2_id, {'network': net_req})

    #end _policy_link_vns

    def _create_two_vns(self, vn1_name = None, vn1_tenant = None,
                              vn2_name = None, vn2_tenant = None):
        if not vn1_name:
            vn1_name = 'vn1'
        if not vn2_name:
            vn2_name = 'vn2'

        print "Creating network %s, subnet 192.168.1.0/24" %(vn1_name)
        net_req = {'name': vn1_name}
        net_rsp = self._quantum.create_network({'network': net_req})
        net1_id = net_rsp['network']['id']
        net1_fq_name = net_rsp['network']['contrail:fq_name']
        net1_fq_name_str = ':'.join(net1_fq_name)
        self._create_subnet(u'192.168.1.0/24', net1_id)

        print "Creating network %s, subnet 192.168.2.0/24" %(vn2_name)
        net_req = {'name': vn2_name}
        net_rsp = self._quantum.create_network({'network': net_req})
        net2_id = net_rsp['network']['id']
        net2_fq_name = net_rsp['network']['contrail:fq_name']
        net2_fq_name_str = ':'.join(net2_fq_name)
        self._create_subnet(u'192.168.2.0/24', net2_id)

        return net1_id, net2_id, net1_fq_name, net2_fq_name
    #end _create_two_vns

    def _create_subnet(self, cidr, net_id, ipam_fq_name = None):
        if not ipam_fq_name:
            ipam_fq_name = NetworkIpam().get_fq_name()

        subnet_req = {'network_id': net_id,
                      'cidr': cidr,
                      'ip_version': 4,
                      'contrail:ipam_fq_name': ipam_fq_name}
        subnet_rsp = self._quantum.create_subnet({'subnet': subnet_req})
        subnet_cidr = subnet_rsp['subnet']['cidr']
        return subnet_rsp['subnet']['id']
    #end _create_subnet

    def _parse_args(self, args_str):
        '''
        Eg. python demo_cfg.py --api_server_ip 127.0.0.1
                               --api_server_port 8082
                               --public_subnet 10.84.41.0/24
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)
        
        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip' : '127.0.0.1',
            'api_server_port' : '8082',
        }
        ksopts = {
            'admin_user'       : 'user1',
            'admin_password'   : 'password1',
            'admin_tenant_name': 'default-domain'
        }

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))

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
        defaults.update(ksopts)
        parser.set_defaults(**defaults)

        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", help = "Port of api server")
        parser.add_argument("--public_subnet", help = "Subnet for public VN")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")
    
        self._args = parser.parse_args(remaining_argv)

    #end _parse_args

#end class DemoCfg

def main(args_str = None):
    DemoCfg(args_str)
#end main

if __name__ == "__main__":
    main()
