#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from sandesh_common.vns.constants import HttpPortTopology, \
    OpServerAdminPort, \
    ServicesDefaultConfigurationFiles, SERVICE_TOPOLOGY
from sandesh_common.vns.ttypes import Module
import traceback
from vnc_api.vnc_api import VncApi

class CfgParser(object):
    CONF_DEFAULT_PATHS = ServicesDefaultConfigurationFiles.get(
        SERVICE_TOPOLOGY, None)
    def __init__(self, argv):
        self._args = None
        self.__pat = None
        self._argv = argv or ' '.join(sys.argv[1:])

    def parse(self):
        '''
            command line example
contrail-topology [-h] [-c FILE]
                         [--analytics_api ANALYTICS_API [ANALYTICS_API ...]]
                         [--collectors COLLECTORS [COLLECTORS ...]]
                         [--log_file LOG_FILE] [--log_local]
                         [--log_category LOG_CATEGORY] [--log_level LOG_LEVEL]
                         [--use_syslog] [--syslog_facility SYSLOG_FACILITY]
                         [--scan_frequency SCAN_FREQUENCY]
                         [--http_server_port HTTP_SERVER_PORT]

optional arguments:
  -h, --help            show this help message and exit
  -c FILE, --conf_file FILE
                        Specify config file
  --analytics_api ANALYTICS_API [ANALYTICS_API ...]
                        List of analytics-api IP addresses in ip:port format
  --collectors COLLECTORS [COLLECTORS ...]
                        List of Collector IP addresses in ip:port format
  --log_file LOG_FILE   Filename for the logs to be written to
  --log_local           Enable local logging of sandesh messages
  --log_category LOG_CATEGORY
                        Category filter for local logging of sandesh messages
  --log_level LOG_LEVEL
                        Severity level for local logging of sandesh messages
  --use_syslog          Use syslog for logging
  --syslog_facility SYSLOG_FACILITY
                        Syslog facility to receive log lines
  --scan_frequency SCAN_FREQUENCY
                        Time between snmp poll
  --http_server_port HTTP_SERVER_PORT
                        introspect server port

        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        kwargs = {'help': "Specify config file", 'metavar':"FILE",
                  'action':'append', 'default': self.CONF_DEFAULT_PATHS,
                 }
        conf_parser.add_argument("-c", "--conf_file", **kwargs)
        args, remaining_argv = conf_parser.parse_known_args(self._argv.split())

        defaults = {
            'collectors'      : None,
            'analytics_api'   : ['127.0.0.1:' + str(OpServerAdminPort)],
            'log_local'       : False,
            'log_level'       : SandeshLevel.SYS_DEBUG,
            'log_category'    : '',
            'log_file'        : Sandesh._DEFAULT_LOG_FILE,
            'use_syslog'      : False,
            'syslog_facility' : Sandesh._DEFAULT_SYSLOG_FACILITY,
            'scan_frequency'  : 60,
            'http_server_port': HttpPortTopology,
            'zookeeper'       : '127.0.0.1:2181',
            'cluster_id'      : '',
        }
        defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
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
        sandesh_opts = SandeshConfig.get_default_options()

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
            SandeshConfig.update_options(sandesh_opts, config)
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
        parser.add_argument("--analytics_api",
            help="List of analytics-api IP addresses in ip:port format",
            nargs="+")
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
            help="Time between snmp poll")
        parser.add_argument("--http_server_port", type=int,
            help="introspect server port")
        parser.add_argument("--zookeeper",
            help="ip:port of zookeeper server")
        parser.add_argument("--cluster_id",
            help="Used for database keyspace separation")
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
        parser.add_argument("--api_server_list",
            help="List of api-servers in ip:port format separated by space",
            nargs="+")
        parser.add_argument("--api_server_use_ssl",
            help="Use SSL to connect to api-server")
        SandeshConfig.add_parser_arguments(parser)

        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if type(self._args.analytics_api) is str:
            self._args.analytics_api = self._args.analytics_api.split()
        if type(self._args.api_server_list) is str:
            self._args.api_server_list = self._args.api_server_list.split()

        self._args.config_sections = config
        self._args.conf_file = args.conf_file

    def _pat(self):
        if self.__pat is None:
           self.__pat = re.compile(', *| +')
        return self.__pat

    def set_api_server_list(self, api_servers):
        self._args.api_server_list = api_servers

    def _mklist(self, s):
        return self._pat().split(s)

    def collectors(self):
        return self._args.collectors

    def api_server_list(self):
        return self._args.api_server_list

    def zookeeper_server(self):
        return self._args.zookeeper

    def cluster_id(self):
        return self._args.cluster_id

    def analytics_api(self):
        return self._args.analytics_api

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

    def frequency(self):
        return self._args.scan_frequency

    def http_port(self):
        return self._args.http_server_port

    def admin_user(self):
        return self._args.admin_user

    def admin_password(self):
        return self._args.admin_password

    def sandesh_config(self):
        return SandeshConfig.from_parser_arguments(self._args)

    def vnc_api(self):
        e = SystemError('Cant connect to API server')
        api_servers = [srv.split(':')[0] for srv in self._args.api_server_list]
        api_server_port = self._args.api_server_list[0].split(':')[1] \
            if self._args.api_server_list else None
        try:
            vnc = VncApi(self._args.admin_user,
                         self._args.admin_password,
                         self._args.admin_tenant_name,
                         api_servers, api_server_port,
                         api_server_use_ssl=self._args.api_server_use_ssl,
                         auth_host=self._args.auth_host,
                         auth_port=self._args.auth_port,
                         auth_protocol=self._args.auth_protocol)
        except Exception as e:
            ConnectionState.update(conn_type=ConnectionType.APISERVER,
                name='Config', status=ConnectionStatus.DOWN, message=str(e),
                server_addrs=api_servers)
            return None
        else:
            ConnectionState.update(conn_type=ConnectionType.APISERVER,
                name='Config', status=ConnectionStatus.UP,
                server_addrs=api_servers)
            return vnc
