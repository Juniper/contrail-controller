import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class CfgParser(object):
    CONF_DEFAULT_PATH = '/etc/contrail/contrail-alarm-gen.conf'
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
                    --conf_file /etc/contrail/contrail-alarm-gen.conf

[DEFAULTS]
log_local = 0
log_level = SYS_DEBUG
log_category =
log_file = /var/log/contrail/contrail-alarm-gen.log

        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        kwargs = {'help': "Specify config file", 'metavar':"FILE"}
        if os.path.exists(self.CONF_DEFAULT_PATH):
            kwargs['default'] = self.CONF_DEFAULT_PATH
        conf_parser.add_argument("-c", "--conf_file", **kwargs)
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

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.optionxform = str
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
            if 'REDIS' in config.sections():
                redis_opts.update(dict(config.items('REDIS')))
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

        defaults.update(redis_opts)
        defaults.update(disc_opts)
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
