#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from device_config import DeviceConfig
import discoveryclient.client as client
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, \
         API_SERVER_DISCOVERY_SERVICE_NAME, HttpPortSnmpCollector

class CfgParser(object):
    CONF_DEFAULT_PATH = '/etc/contrail/contrail-snmp-collector.conf'
    def __init__(self, argv):
        self._devices = []
        self._args = None
        self.__pat = None
        self._argv = argv or ' '.join(sys.argv[1:])
        self._name = ModuleNames[Module.CONTRAIL_SNMP_COLLECTOR]
        self._disc = None
        self._cb = None

    def set_cb(self, cb=None):
        self._cb = cb

    def parse(self):
        '''
            command line example
contrail-snmp-scanner --log_level SYS_DEBUG
                      --logging_level DEBUG
                      --log_category test
                      --log_file <stdout>
                      --use_syslog
                      --syslog_facility LOG_USER
                      --disc_server_ip 127.0.0.1
                      --disc_server_port 5998
                      --conf_file /etc/contrail/contrail-snmp-scanner.conf

            conf file example:


[DEFAULTS]
log_local = 0
log_level = SYS_DEBUG
log_category =
log_file = /var/log/contrail/contrail-analytics-api.log
file = /etc/contrail/snmp-dev.ini

            /etc/contrail/snmp-dev.ini example:

#snmp version 1 or 2
[1.1.1.190]
Community = public
Version = 2

#snmp version 3
[1.1.1.191]
Version = 3
SecLevel = authPriv
AuthProto = SHA
AuthPass = foo
PrivProto = AES
PrivPass = foo
SecName = snmpuser
# Mibs default to all, to get a subset
Mibs = LldpTable, ArpTable

        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        kwargs = {'help': "Specify config file", 'metavar':"FILE",
                'action':'append'}
        if os.path.exists(self.CONF_DEFAULT_PATH):
            kwargs['default'] = [self.CONF_DEFAULT_PATH]
        conf_parser.add_argument("-c", "--conf_file", **kwargs)
        args, remaining_argv = conf_parser.parse_known_args(self._argv.split())

        defaults = {
            'collectors'          : None,
            'log_local'           : False,
            'log_level'           : SandeshLevel.SYS_DEBUG,
            'log_category'        : '',
            'log_file'            : Sandesh._DEFAULT_LOG_FILE,
            'use_syslog'          : False,
            'syslog_facility'     : Sandesh._DEFAULT_SYSLOG_FACILITY,
            'scan_frequency'      : 600,
            'fast_scan_frequency' : 60,
            'http_server_port'    : HttpPortSnmpCollector,
            'zookeeper'           : '127.0.0.1:2181',
            'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
            'cluster_id'          :'',
        }
        ksopts = {
            'auth_host': '127.0.0.1',
            'auth_protocol': 'http',
            'auth_port': 35357,
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }
        disc_opts = {
            'disc_server_ip'     : '127.0.0.1',
            'disc_server_port'   : 5998,
        }

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.optionxform = str
            config.read(args.conf_file)
            if 'DEFAULTS' in config.sections():
                defaults.update(dict(config.items("DEFAULTS")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))
            if 'DISCOVERY' in config.sections():
                disc_opts.update(dict(config.items('DISCOVERY')))
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
        defaults.update(disc_opts)
        parser.set_defaults(**defaults)
        parser.add_argument("--collectors",
            help="List of Collector IP addresses in ip:port format",
            nargs="+")
        parser.add_argument(
            "--log_file",
            help="Filename for the logs to be written to")
        parser.add_argument("--log_local", action="store_true",
            help="Enable local logging of sandesh messages")
        parser.add_argument(
            "--log_category",
            help="Category filter for local logging of sandesh messages")
        parser.add_argument(
            "--log_level",
            help="Severity level for local logging of sandesh messages")
        parser.add_argument("--use_syslog",
            action="store_true",
            help="Use syslog for logging")
        parser.add_argument("--syslog_facility",
            help="Syslog facility to receive log lines")
        parser.add_argument("--scan_frequency", type=int,
            help="Time between snmp full poll")
        parser.add_argument("--fast_scan_frequency", type=int,
            help="Time between snmp interface status poll")
        parser.add_argument("--http_server_port", type=int,
            help="introspect server port")
        parser.add_argument("--auth_host",
                            help="ip of keystone server")
        parser.add_argument("--auth_protocol",
                            help="keystone authentication protocol")
        parser.add_argument("--auth_port", type=int,
                            help="ip of keystone server")
        parser.add_argument("--admin_user",
                            help="Name of keystone admin user")
        parser.add_argument("--admin_password",
                            help="Password of keystone admin user")
        parser.add_argument("--admin_tenant_name",
                            help="Tenant name for keystone admin user")
        parser.add_argument("--zookeeper",
            help="ip:port of zookeeper server")
        parser.add_argument("--cluster_id",
            help="Used for database keyspace separation")
        parser.add_argument("--disc_server_ip",
            help="Discovery Server IP address")
        parser.add_argument("--disc_server_port", type=int,
            help="Discovery Server port")
        group = parser.add_mutually_exclusive_group(required=False)
        group.add_argument("--device-config-file",
            help="where to look for snmp credentials")
        group.add_argument("--api_server",
            help="ip:port of api-server for snmp credentials")
        group.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec.")
        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        self._args.config_sections = config
        self._disc = client.DiscoveryClient(*self.discovery_params())

    def devices(self):
        if self._args.device_config_file:
            self._devices = DeviceConfig.fom_file(
                    self._args.device_config_file)
        elif self._args.api_server:
            self._devices = DeviceConfig.fom_api_server(
                    [self._args.api_server],
                    self._args.admin_user, self._args.admin_password,
                    self._args.admin_tenant_name,
                    self._args.auth_host, self._args.auth_port,
                    self._args.auth_protocol, self._cb)
        elif self._args.disc_server_port:
            apis = self.get_api_svrs()
            if apis:
                self._devices = DeviceConfig.fom_api_server(
                    apis, self._args.admin_user,
                    self._args.admin_password, self._args.admin_tenant_name,
                    self._args.auth_host, self._args.auth_port,
                    self._args.auth_protocol, self._cb)
            else:
                self._devices = []
        return self._devices

    def get_api_svrs(self):
        if self._disc is None:
            p = self.discovery_params()
            try:
              self._disc = client.DiscoveryClient(*p)
            except Exception as e:
              import traceback; traceback.print_exc()
              return []
        a = self._disc.subscribe(API_SERVER_DISCOVERY_SERVICE_NAME, 0)
        x = a.read()
        return map(lambda d:d['ip-address'] + ':' + d['port'], x)

    def discovery_params(self):
        if self._args.disc_server_ip:
            ip, port = self._args.disc_server_ip, \
                       self._args.disc_server_port
        else:
            ip, port = '127.0.0.1', self._args.disc_server_port
        return ip, port, self._name

    def collectors(self):
        return self._args.collectors

    def zookeeper_server(self):
        return self._args.zookeeper

    def cluster_id(self):
        return self._args.cluster_id
 
    def log_local(self):
        return self._args.log_local

    def log_category(self):
        return self._args.log_category

    def log_level(self):
        return self._args.log_level

    def log_file(self):
        return self._args.log_file

    def use_syslog(self):
        return self._args.use_syslog

    def syslog_facility(self):
        return self._args.syslog_facility

    def fast_scan_freq(self):
        return self._args.fast_scan_frequency

    def frequency(self):
        return self._args.scan_frequency

    def http_port(self):
        return self._args.http_server_port

    def sandesh_send_rate_limit(self):
        return self._args.sandesh_send_rate_limit
