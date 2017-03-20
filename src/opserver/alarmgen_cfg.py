#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import SERVICE_ALARM_GENERATOR, \
    ServicesDefaultConfigurationFiles

class CfgParser(object):

    def __init__(self, argv):
        self._devices = []
        self._args = None
        self.__pat = None
        self._argv = argv or ' '.join(sys.argv[1:])

    def parse(self):
        '''
            command line example
            contrail-alarm-gen  --log_level SYS_DEBUG
                    --logging_level DEBUG
                    --log_category test
                    --log_file <stdout>
                    --use_syslog
                    --syslog_facility LOG_USER
                    --worker_id 0
                    --partitions 5
                    --redis_password
                    --http_server_port 5995
                    --redis_server_port 6379
                    --redis_uve_list 127.0.0.1:6379
                    --alarmgen_list 127.0.0.1:0
                    --kafka_broker_list 127.0.0.1:9092
                    --zk_list 127.0.0.1:2181
                    --rabbitmq_server_list 127.0.0.1:5672
                    --conf_file /etc/contrail/contrail-alarm-gen.conf
        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)
        conf_parser.add_argument("-c", "--conf_file", action="append",
            help="Specify config file", metavar="FILE",
            default=ServicesDefaultConfigurationFiles.get(
                SERVICE_ALARM_GENERATOR, None))
        args, remaining_argv = conf_parser.parse_known_args(self._argv.split())

        defaults = {
            'host_ip'           : '127.0.0.1',
            'collectors'        : [],
            'kafka_broker_list' : ['127.0.0.1:9092'],
            'log_local'         : False,
            'log_level'         : SandeshLevel.SYS_DEBUG,
            'log_category'      : '',
            'log_file'          : Sandesh._DEFAULT_LOG_FILE,
            'use_syslog'        : False,
            'syslog_facility'   : Sandesh._DEFAULT_SYSLOG_FACILITY,
            'http_server_port'  : 5995,
            'worker_id'         : '0',
            'partitions'        : 15,
            'zk_list'           : None,
            'rabbitmq_server_list' : None,
            'rabbitmq_port'     : 5672,
            'rabbitmq_user'     : 'guest',
            'rabbitmq_password' : 'guest',
            'rabbitmq_vhost'    : None,
            'rabbitmq_ha_mode'  : False,
            'rabbitmq_use_ssl'  : False,
            'kombu_ssl_version': '',
            'kombu_ssl_keyfile': '',
            'kombu_ssl_certfile': '',
            'kombu_ssl_ca_certs': '',
            'alarmgen_list'     : ['127.0.0.1:0'],
            'sandesh_send_rate_limit' : SandeshSystem.get_sandesh_send_rate_limit(),
            'cluster_id'     :'',
        }

        api_opts = {
            'api_server_list' : ['127.0.0.1:8082'],
            'api_server_use_ssl' : False
        }

        redis_opts = {
            'redis_server_port'  : 6379,
            'redis_password'     : None,
            'redis_uve_list'    : ['127.0.0.1:6379'],
        }

        keystone_opts = {
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
                defaults.update(dict(config.items('DEFAULTS')))
            if 'API_SERVER' in config.sections():
                api_opts.update(dict(config.items('API_SERVER')))
            if 'REDIS' in config.sections():
                redis_opts.update(dict(config.items('REDIS')))
            if 'KEYSTONE' in config.sections():
                keystone_opts.update(dict(config.items('KEYSTONE')))
            if 'SANDESH' in config.sections():
                sandesh_opts.update(dict(config.items('SANDESH')))
                if 'sandesh_ssl_enable' in config.options('SANDESH'):
                    sandesh_opts['sandesh_ssl_enable'] = config.getboolean(
                        'sandesh', 'sandesh_ssl_enable')
                if 'introspect_ssl_enable' in config.options('SANDESH'):
                    sandesh_opts['introspect_ssl_enable'] = config.getboolean(
                        'sandesh', 'introspect_ssl_enable')
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
        defaults.update(redis_opts)
        defaults.update(keystone_opts)
        defaults.update(sandesh_opts)
        parser.set_defaults(**defaults)
        parser.add_argument("--host_ip",
            help="Host IP address")
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
        parser.add_argument("--http_server_port", type=int,
            help="introspect server port")
        parser.add_argument("--worker_id",
            help="Worker Id")
        parser.add_argument("--partitions", type=int,
            help="Number of partitions for hashing UVE keys")
        parser.add_argument("--redis_server_port",
            type=int,
            help="Redis server port")
        parser.add_argument("--redis_password",
            help="Redis server password")
        parser.add_argument("--kafka_broker_list",
            help="List of bootstrap kafka brokers in ip:port format",
            nargs="+")
        parser.add_argument("--zk_list",
            help="List of zookeepers in ip:port format",
            nargs="+")
        parser.add_argument("--rabbitmq_server_list", type=str,
            help="List of Rabbitmq server ip address separated by comma")
        parser.add_argument("--rabbitmq_port",
            help="Rabbitmq server port")
        parser.add_argument("--rabbitmq_user",
            help="Username for Rabbitmq")
        parser.add_argument("--rabbitmq_password",
            help="Password for Rabbitmq")
        parser.add_argument("--rabbitmq_vhost",
            help="vhost for Rabbitmq")
        parser.add_argument("--rabbitmq_ha_mode",
            action="store_true",
            help="True if the rabbitmq cluster is mirroring all queue")
        parser.add_argument("--redis_uve_list",
            help="List of redis-uve in ip:port format. For internal use only",
            nargs="+")
        parser.add_argument("--alarmgen_list",
            help="List of alarmgens in ip:inst format. For internal use only",
            nargs="+")
        parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec")
        parser.add_argument("--cluster_id",
            help="Analytics Cluster Id")
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
        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if type(self._args.kafka_broker_list) is str:
            self._args.kafka_broker_list= self._args.kafka_broker_list.split()
        if type(self._args.zk_list) is str:
            self._args.zk_list= self._args.zk_list.split()
        if type(self._args.redis_uve_list) is str:
            self._args.redis_uve_list = self._args.redis_uve_list.split()
        if type(self._args.alarmgen_list) is str:
            self._args.alarmgen_list = self._args.alarmgen_list.split()
        if type(self._args.api_server_list) is str:
            self._args.api_server_list = self._args.api_server_list.split()
        self._args.conf_file = args.conf_file

    def _pat(self):
        if self.__pat is None:
           self.__pat = re.compile(', *| +')
        return self.__pat

    def _mklist(self, s):
        return self._pat().split(s)

    def redis_uve_list(self):
        return self._args.redis_uve_list

    def alarmgen_list(self):
        return self._args.alarmgen_list

    def collectors(self):
        return self._args.collectors

    def kafka_broker_list(self):
        return self._args.kafka_broker_list

    def zk_list(self):
        return self._args.zk_list;

    def api_server_config(self):
        return {
            'api_server_list': self._args.api_server_list,
            'api_server_use_ssl': self._args.api_server_use_ssl
        }

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

    def http_port(self):
        return self._args.http_server_port

    def worker_id(self):
        return self._args.worker_id

    def partitions(self):
        return self._args.partitions

    def redis_password(self):
        return self._args.redis_password

    def redis_server_port(self):
        return self._args.redis_server_port

    def host_ip(self):
        return self._args.host_ip

    def sandesh_send_rate_limit(self):
        return self._args.sandesh_send_rate_limit

    def kafka_prefix(self):
        return self._args.cluster_id

    def rabbitmq_params(self):
        return {'servers': self._args.rabbitmq_server_list,
                'port': self._args.rabbitmq_port,
                'user': self._args.rabbitmq_user,
                'password': self._args.rabbitmq_password,
                'vhost': self._args.rabbitmq_vhost,
                'ha_mode': self._args.rabbitmq_ha_mode,
                'use_ssl': self._args.rabbitmq_use_ssl,
                'ssl_version': self._args.kombu_ssl_version,
                'ssl_keyfile': self._args.kombu_ssl_keyfile,
                'ssl_certfile': self._args.kombu_ssl_certfile,
                'ssl_ca_certs': self._args.kombu_ssl_ca_certs}

    def keystone_params(self):
        return {'auth_host': self._args.auth_host,
                'auth_protocol': self._args.auth_protocol,
                'auth_port': self._args.auth_port,
                'admin_user': self._args.admin_user,
                'admin_password': self._args.admin_password,
                'admin_tenant_name': self._args.admin_tenant_name}

    def sandesh_config(self):
        return SandeshConfig(self._args.sandesh_keyfile,
                             self._args.sandesh_certfile,
                             self._args.sandesh_ca_cert,
                             self._args.sandesh_ssl_enable,
                             self._args.introspect_ssl_enable)
