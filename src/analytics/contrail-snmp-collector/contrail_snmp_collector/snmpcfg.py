#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from device_config import DeviceConfig
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, \
    HttpPortSnmpCollector, \
    ServicesDefaultConfigurationFiles, SERVICE_SNMP_COLLECTOR

class CfgParser(object):
    CONF_DEFAULT_PATHS = ServicesDefaultConfigurationFiles.get(
        SERVICE_SNMP_COLLECTOR, None)
    def __init__(self, argv):
        self._devices = []
        self._args = None
        self.__pat = None
        self._argv = argv or ' '.join(sys.argv[1:])
        self._name = ModuleNames[Module.CONTRAIL_SNMP_COLLECTOR]
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
                'action':'append', 'default':self.CONF_DEFAULT_PATHS}
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
        api_opts = {
            'api_server_list' : ['127.0.0.1:8082'],
            'api_server_use_ssl' : False
        }
        ksopts = {
            'auth_host': '127.0.0.1',
            'auth_protocol': 'http',
            'auth_port': 35357,
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }
        sandesh_opts = {
            'sandesh_keyfile': '/etc/contrail/ssl/private/server-privkey.pem',
            'sandesh_certfile': '/etc/contrail/ssl/certs/server.pem',
            'sandesh_ca_cert': '/etc/contrail/ssl/certs/ca-cert.pem',
            'sandesh_ssl_enable': False,
            'introspect_ssl_enable': False
        }

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.optionxform = str
            config.read(args.conf_file)
            if 'DEFAULTS' in config.sections():
                defaults.update(dict(config.items("DEFAULTS")))
            if 'API_SERVER' in config.sections():
                api_opts.update(dict(config.items("API_SERVER")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))
            if 'SANDESH' in config.sections():
                sandesh_opts.update(dict(config.items('SANDESH')))
                if 'sandesh_ssl_enable' in config.options('SANDESH'):
                    sandesh_opts['sandesh_ssl_enable'] = config.getboolean(
                        'SANDESH', 'sandesh_ssl_enable')
                if 'introspect_ssl_enable' in config.options('SANDESH'):
                    sandesh_opts['introspect_ssl_enable'] = config.getboolean(
                        'SANDESH', 'introspect_ssl_enable')
        # Override with CLI options
        # Don't surpress add_help here so it will handle -h
        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            # Don't mess with format of description
            formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        )
        defaults.update(api_opts)
        defaults.update(ksopts)
        defaults.update(sandesh_opts)
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
        parser.add_argument("--sandesh_keyfile",
            help="Sandesh ssl private key")
        parser.add_argument("--sandesh_certfile",
            help="Sandesh ssl certificate")
        parser.add_argument("--sandesh_ca_cert",
            help="Sandesh CA ssl certificate")
        parser.add_argument("--sandesh_ssl_enable", action="store_true",
            help="Enable ssl for sandesh connection")
        parser.add_argument("--introspect_ssl_enable", action="store_true",
            help="Enable ssl for introspect connection")
        parser.add_argument("--api_server_list",
            help="List of api-servers in ip:port format separated by space",
            nargs="+")
        parser.add_argument("--api_server_use_ssl",
            help="Use SSL to connect to api-server")
        group = parser.add_mutually_exclusive_group(required=False)
        group.add_argument("--device-config-file",
            help="where to look for snmp credentials")
        group.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec.")
        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if type(self._args.api_server_list) is str:
            self._args.api_server_list = self._args.api_server_list.split()
        self._args.config_sections = config
        self._args.conf_file = args.conf_file

    def devices(self):
        if self._args.device_config_file:
            self._devices = DeviceConfig.fom_file(
                    self._args.device_config_file)
        elif self._args.api_server_list:
            self._devices = DeviceConfig.fom_api_server(
                    self._args.api_server_list,
                    self._args.admin_user, self._args.admin_password,
                    self._args.admin_tenant_name,
                    self._args.api_server_use_ssl,
                    self._args.auth_host, self._args.auth_port,
                    self._args.auth_protocol, self._cb)
        return self._devices

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

    def sandesh_config(self):
        return SandeshConfig(self._args.sandesh_keyfile,
                             self._args.sandesh_certfile,
                             self._args.sandesh_ca_cert,
                             self._args.sandesh_ssl_enable,
                             self._args.introspect_ssl_enable)
