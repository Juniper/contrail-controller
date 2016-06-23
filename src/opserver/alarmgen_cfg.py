import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

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
                    --disc_server_ip 127.0.0.1
                    --disc_server_port 5998
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
            help="Specify config file", metavar="FILE")
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
            'redis_uve_list'    : ['127.0.0.1:6379'],
            'alarmgen_list'     : ['127.0.0.1:0'],
            'sandesh_send_rate_limit' : SandeshSystem.get_sandesh_send_rate_limit(),
            'kafka_prefix'     :'',
        }

        redis_opts = {
            'redis_server_port'  : 6379,
            'redis_password'     : None,
        }

        disc_opts = {
            'disc_server_ip'     : None,
            'disc_server_port'   : 5998,
        }

        keystone_opts = {
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
                defaults.update(dict(config.items('DEFAULTS')))
            if 'REDIS' in config.sections():
                redis_opts.update(dict(config.items('REDIS')))
            if 'DISCOVERY' in config.sections():
                disc_opts.update(dict(config.items('DISCOVERY')))
            if 'KEYSTONE' in config.sections():
                keystone_opts.update(dict(config.items('KEYSTONE')))
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

        defaults.update(redis_opts)
        defaults.update(disc_opts)
        defaults.update(keystone_opts)
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
        parser.add_argument("--disc_server_ip",
            help="Discovery Server IP address")
        parser.add_argument("--disc_server_port",
            type=int,
            help="Discovery Server port")
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
        parser.add_argument("--kafka_prefix",
            help="System Prefix for Kafka")
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
        if type(self._args.kafka_broker_list) is str:
            self._args.kafka_broker_list= self._args.kafka_broker_list.split()
        if type(self._args.zk_list) is str:
            self._args.zk_list= self._args.zk_list.split()
        if type(self._args.redis_uve_list) is str:
            self._args.redis_uve_list = self._args.redis_uve_list.split()
        if type(self._args.redis_uve_list) is str:
            self._args.alarmgen_list = self._args.alarmgen_list.split()

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

    def discovery(self):
        return {'server':self._args.disc_server_ip,
            'port':self._args.disc_server_port }

    def collectors(self):
        return self._args.collectors

    def kafka_broker_list(self):
        return self._args.kafka_broker_list

    def zk_list(self):
        return self._args.zk_list;

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
        return self._args.kafka_prefix

    def rabbitmq_params(self):
        return {'servers': self._args.rabbitmq_server_list,
                'port': self._args.rabbitmq_port,
                'user': self._args.rabbitmq_user,
                'password': self._args.rabbitmq_password,
                'vhost': self._args.rabbitmq_vhost,
                'ha_mode': self._args.rabbitmq_ha_mode}

    def keystone_params(self):
        return {'auth_host': self._args.auth_host,
                'auth_protocol': self._args.auth_protocol,
                'auth_port': self._args.auth_port,
                'admin_user': self._args.admin_user,
                'admin_password': self._args.admin_password,
                'admin_tenant_name': self._args.admin_tenant_name}
