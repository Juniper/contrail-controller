#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
"""
Provides utility routines for modules in api-server
"""
import sys
import argparse
from cfgm_common import jsonutils as json
import ConfigParser
import gen.resource_xsd
import vnc_quota
from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

_WEB_HOST = '0.0.0.0'
_WEB_PORT = 8082
_ADMIN_PORT = 8095
_CLOUD_ADMIN_ROLE = 'admin'

def parse_args(args_str):
    args_obj = None
    # Source any specified config/ini file
    # Turn off help, so we print all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'reset_config': False,
        'wipe_config': False,
        'listen_ip_addr': _WEB_HOST,
        'listen_port': _WEB_PORT,
        'admin_port': _ADMIN_PORT,
        'ifmap_server_ip': '127.0.0.1',
        'ifmap_server_port': "8443",
        'ifmap_queue_size': 10000,
        'ifmap_max_message_size': 1024*1024,
        'cassandra_server_list': "127.0.0.1:9160",
        'ifmap_username': "api-server",
        'ifmap_password': "api-server",
        'collectors': None,
        'http_server_port': '8084',
        'log_local': True,
        'log_level': SandeshLevel.SYS_NOTICE,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'trace_file': '/var/log/contrail/vnc_openstack.err',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'logging_level': 'WARN',
        'logging_conf': '',
        'logger_class': None,
        'multi_tenancy': True,
        'multi_tenancy_with_rbac': True,
        'disc_server_ip': None,
        'disc_server_port': '5998',
        'zk_server_ip': '127.0.0.1:2181',
        'worker_id': '0',
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'rabbit_max_pending_updates': '4096',
        'cluster_id': '',
        'max_requests': 1024,
        'region_name': 'RegionOne',
        'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
        'ifmap_health_check_interval': '60', # in seconds
        'stale_lock_seconds': '5', # lock but no resource past this => stale
        'cloud_admin_role': _CLOUD_ADMIN_ROLE,
        'rabbit_use_ssl': False,
        'kombu_ssl_version': '',
        'kombu_ssl_keyfile': '',
        'kombu_ssl_certfile': '',
        'kombu_ssl_ca_certs': '',
    }
    # ssl options
    secopts = {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
        'ifmap_certauth_port': "8444",
    }
    # keystone options
    ksopts = {
        'auth_host': '127.0.0.1',
        'auth_port': '35357',
        'auth_protocol': 'http',
        'admin_user': '',
        'admin_password': '',
        'admin_tenant_name': '',
        'insecure': True
    }
    # cassandra options
    cassandraopts = {
        'cassandra_user'     : None,
        'cassandra_password' : None
    }


    config = None
    if args.conf_file:
        config = ConfigParser.SafeConfigParser({'admin_token': None})
        config.read(args.conf_file)
        if 'DEFAULTS' in config.sections():
            defaults.update(dict(config.items("DEFAULTS")))
            if 'multi_tenancy' in config.options('DEFAULTS'):
                defaults['multi_tenancy'] = config.getboolean(
                    'DEFAULTS', 'multi_tenancy')
            if 'multi_tenancy_with_rbac' in config.options('DEFAULTS'):
                defaults['multi_tenancy_with_rbac'] = config.getboolean('DEFAULTS', 'multi_tenancy_with_rbac')
            if 'default_encoding' in config.options('DEFAULTS'):
                default_encoding = config.get('DEFAULTS', 'default_encoding')
                gen.resource_xsd.ExternalEncoding = default_encoding
        if 'SECURITY' in config.sections() and\
                'use_certs' in config.options('SECURITY'):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))
        if 'QUOTA' in config.sections():
            for (k, v) in config.items("QUOTA"):
                try:
                    if str(k) != 'admin_token':
                        vnc_quota.QuotaHelper.default_quota[str(k)] = int(v)
                except ValueError:
                    pass
        if 'CASSANDRA' in config.sections():
                cassandraopts.update(dict(config.items('CASSANDRA')))

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
    defaults.update(secopts)
    defaults.update(ksopts)
    defaults.update(cassandraopts)
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--ifmap_server_ip", help="IP address of ifmap server")
    parser.add_argument(
        "--ifmap_server_port", help="Port of ifmap server")
    parser.add_argument(
        "--ifmap_queue_size", type=int, help="Size of the queue that holds "
        "pending messages to be sent to ifmap server")
    parser.add_argument(
        "--ifmap_max_message_size", type=int, help="Maximum size of message "
        "sent to ifmap server")

    # TODO should be from certificate
    parser.add_argument(
        "--ifmap_username",
        help="Username known to ifmap server")
    parser.add_argument(
        "--ifmap_password",
        help="Password known to ifmap server")
    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--disc_server_ip",
        help="IP address of discovery server")
    parser.add_argument(
        "--disc_server_port",
        help="Port of discovery server")
    parser.add_argument(
        "--redis_server_ip",
        help="IP address of redis server")
    parser.add_argument(
        "--redis_server_port",
        help="Port of redis server")
    parser.add_argument(
        "--auth", choices=['keystone'],
        help="Type of authentication for user-requests")
    parser.add_argument(
        "--reset_config", action="store_true",
        help="Warning! Destroy previous configuration and start clean")
    parser.add_argument(
        "--wipe_config", action="store_true",
        help="Warning! Destroy previous configuration")
    parser.add_argument(
        "--listen_ip_addr",
        help="IP address to provide service on, default %s" % (_WEB_HOST))
    parser.add_argument(
        "--listen_port",
        help="Port to provide service on, default %s" % (_WEB_PORT))
    parser.add_argument(
        "--admin_port",
        help="Port with local auth for admin access, default %s"
              % (_ADMIN_PORT))
    parser.add_argument(
        "--collectors",
        help="List of VNC collectors in ip:port format",
        nargs="+")
    parser.add_argument(
        "--http_server_port",
        help="Port of local HTTP server")
    parser.add_argument(
        "--ifmap_server_loc",
        help="Location of IFMAP server")
    parser.add_argument(
        "--log_local", action="store_true",
        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--logging_level",
        help=("Log level for python logging: DEBUG, INFO, WARN, ERROR default: %s"
              % defaults['logging_level']))
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument(
        "--log_file",
        help="Filename for the logs to be written to")
    parser.add_argument(
        "--trace_file",
        help="Filename for the errors backtraces to be written to")
    parser.add_argument("--use_syslog",
        action="store_true",
        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
        help="Syslog facility to receive log lines")
    parser.add_argument(
        "--multi_tenancy", action="store_true",
        help="Validate resource permissions (implies token validation)")
    parser.add_argument(
        "--multi_tenancy_with_rbac", action="store_true",
        help="Validate API and resource permissions (implies token validation)")
    parser.add_argument(
        "--worker_id",
        help="Worker Id")
    parser.add_argument(
        "--zk_server_ip",
        help="Ip address:port of zookeeper server")
    parser.add_argument(
        "--rabbit_server",
        help="Rabbitmq server address")
    parser.add_argument(
        "--rabbit_port",
        help="Rabbitmq server port")
    parser.add_argument(
        "--rabbit_user",
        help="Username for rabbit")
    parser.add_argument(
        "--rabbit_vhost",
        help="vhost for rabbit")
    parser.add_argument(
        "--rabbit_password",
        help="password for rabbit")
    parser.add_argument(
        "--rabbit_ha_mode",
        help="True if the rabbitmq cluster is mirroring all queue")
    parser.add_argument(
        "--rabbit_max_pending_updates",
        help="Max updates before stateful changes disallowed")
    parser.add_argument(
        "--cluster_id",
        help="Used for database keyspace separation")
    parser.add_argument(
        "--max_requests", type=int,
        help="Maximum number of concurrent requests served by api server")
    parser.add_argument("--cassandra_user",
            help="Cassandra user name")
    parser.add_argument("--cassandra_password",
            help="Cassandra password")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec.")
    parser.add_argument("--ifmap_health_check_interval",
            help="Interval seconds to check for ifmap health, default 60")
    parser.add_argument("--stale_lock_seconds",
            help="Time after which lock without resource is stale, default 60")
    parser.add_argument( "--cloud_admin_role",
        help="Role name of cloud administrator")
    args_obj, remaining_argv = parser.parse_known_args(remaining_argv)
    args_obj.config_sections = config
    if type(args_obj.cassandra_server_list) is str:
        args_obj.cassandra_server_list =\
            args_obj.cassandra_server_list.split()
    if type(args_obj.collectors) is str:
        args_obj.collectors = args_obj.collectors.split()

    return args_obj, remaining_argv
# end parse_args

try:
    from termcolor import colored
except ImportError:
    def colored(logmsg, *args, **kwargs):
        return logmsg

class ColorLog(object):

    colormap = dict(
        debug=dict(color='green'),
        info=dict(color='green', attrs=['bold']),
        warn=dict(color='yellow', attrs=['bold']),
        warning=dict(color='yellow', attrs=['bold']),
        error=dict(color='red'),
        critical=dict(color='red', attrs=['bold']),
    )

    def __init__(self, logger):
        self._log = logger

    def __getattr__(self, name):
        if name in ['debug', 'info', 'warn', 'warning', 'error', 'critical']:
            return lambda s, *args: getattr(self._log, name)(
                colored(s, **self.colormap[name]), *args)

        return getattr(self._log, name)
# end ColorLog


def get_filters(data, skips=None):
    """Extracts the filters of query parameters.
    Returns a dict of lists for the filters:
    check=a&check=b&name=Bob&
    becomes:
    {'check': [u'a', u'b'], 'name': [u'Bob']}
    'data' contains filters in format:
    check==a,check==b,name==Bob
    """
    skips = skips or []
    res = {}

    if not data:
        return res

    for filter in data.split(','):
        key, value = filter.split('==')
        try:
            value = json.loads(value)
        except ValueError:
            pass
        if key in skips:
            continue
        values = list(set(res.get(key, [])) | set([value]))
        if values:
            res[key] = values
    return res
