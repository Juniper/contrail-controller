#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import ModuleNames, HttpPortTopology, API_SERVER_DISCOVERY_SERVICE_NAME
from sandesh_common.vns.ttypes import Module
import discoveryclient.client as discovery_client
import traceback
from vnc_api.vnc_api import VncApi

class CfgParser(object):
    CONF_DEFAULT_PATH = '/etc/contrail/contrail-topology.conf'
    def __init__(self, argv):
        self._args = None
        self.__pat = None
        self._argv = argv or ' '.join(sys.argv[1:])
        self._disc = None

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
                         [--disc_server_ip 127.0.0.1]
                         [--disc_server_port 5998]

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
                  'action':'append'
                 }
        if os.path.exists(self.CONF_DEFAULT_PATH):
            kwargs['default'] = [self.CONF_DEFAULT_PATH]
        conf_parser.add_argument("-c", "--conf_file", **kwargs)
        args, remaining_argv = conf_parser.parse_known_args(self._argv.split())

        defaults = {
            'collectors'      : None,
            'analytics_api'   : ['127.0.0.1:8081'],
            'log_local'       : False,
            'log_level'       : SandeshLevel.SYS_DEBUG,
            'log_category'    : '',
            'log_file'        : Sandesh._DEFAULT_LOG_FILE,
            'use_syslog'      : False,
            'syslog_facility' : Sandesh._DEFAULT_SYSLOG_FACILITY,
            'scan_frequency'  : 60,
            'http_server_port': HttpPortTopology,
            'zookeeper'       : '127.0.0.1:2181',
            'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
            'cluster_id'      : '',
        }
        disc_opts = {
            'disc_server_ip'     : '127.0.0.1',
            'disc_server_port'   : 5998,
        }
        ksopts = {
            'auth_host': '127.0.0.1',
            'auth_protocol': 'http',
            'auth_port': 35357,
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.optionxform = str
            config.read(args.conf_file)
            if 'DEFAULTS' in config.sections():
                defaults.update(dict(config.items("DEFAULTS")))
            if 'DISCOVERY' in config.sections():
                disc_opts.update(dict(config.items('DISCOVERY')))
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
        defaults.update(disc_opts)
        defaults.update(ksopts)
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
        parser.add_argument("--disc_server_ip",
            help="Discovery Server IP address")
        parser.add_argument("--disc_server_port", type=int,
            help="Discovery Server port")
        parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec.")
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

        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if type(self._args.analytics_api) is str:
            self._args.analytics_api = self._args.analytics_api.split()

        self._args.config_sections = config
        self._disc = discovery_client.DiscoveryClient(
                        self._args.disc_server_ip,
                        self._args.disc_server_port,
                        ModuleNames[Module.CONTRAIL_TOPOLOGY])

    def _pat(self):
        if self.__pat is None:
           self.__pat = re.compile(', *| +')
        return self.__pat

    def _mklist(self, s):
        return self._pat().split(s)

    def collectors(self):
        return self._args.collectors

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

    def sandesh_send_rate_limit(self):
        return self._args.sandesh_send_rate_limit

    def api_svrs(self):
        a = self._disc.subscribe(API_SERVER_DISCOVERY_SERVICE_NAME, 0)
        x = a.read()
        return map(lambda d:d['ip-address'] + ':' + d['port'], x)

    def vnc_api(self, notifycb=None):
        e = SystemError('Cant connect to API server')
        for rt in (5, 2, 7, 9, 16, 25):
            for api_server in self.api_svrs():
                srv = api_server.split(':')
                if len(srv) == 2:
                    ip, port = srv[0], int(srv[1])
                else:
                    ip, port = '127.0.0.1', int(srv[0])
                try:
                    vnc = VncApi(self._args.admin_user,
                                 self._args.admin_password,
                                 self._args.admin_tenant_name,
                                 auth_host=self._args.auth_host,
                                 auth_port=self._args.auth_port,
                                 auth_protocol=self._args.auth_protocol)
                    if callable(notifycb):
                        notifycb('api', 'Connected', servers=api_server)
                    return vnc
                except Exception as e:
                    traceback.print_exc()
                    if callable(notifycb):
                        notifycb('api', 'Not connected', servers=api_server,
                                up=False)
                    time.sleep(rt)
        raise e
