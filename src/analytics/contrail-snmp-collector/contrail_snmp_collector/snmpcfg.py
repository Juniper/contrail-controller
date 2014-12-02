import argparse, os, ConfigParser, sys, re
from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from device_config import DeviceConfig

class CfgParser(object):
    CONF_DEFAULT_PATH = '/etc/contrail/contrail-snmp-scanner.conf'
    def __init__(self, argv):
        self._devices = []
        self._args = None
        self.__pat = None
        self._argv = argv or ' '.join(sys.argv[1:])

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

        kwargs = {'help': "Specify config file", 'metavar':"FILE"}
        if os.path.exists(self.CONF_DEFAULT_PATH):
            kwargs['default'] = self.CONF_DEFAULT_PATH
        conf_parser.add_argument("-c", "--conf_file", **kwargs)
        args, remaining_argv = conf_parser.parse_known_args(self._argv.split())

        defaults = {
            'collectors'      : ['127.0.0.1:8086'],
            'log_local'       : False,
            'log_level'       : SandeshLevel.SYS_DEBUG,
            'log_category'    : '',
            'log_file'        : Sandesh._DEFAULT_LOG_FILE,
            'use_syslog'      : False,
            'syslog_facility' : Sandesh._DEFAULT_SYSLOG_FACILITY,
            'scan_frequency'  : 600,
            'http_server_port': 5920,
            'file'            : 'devices.ini',
        }

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.optionxform = str
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
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
            help="Time between snmp poll")
        parser.add_argument("--http_server_port", type=int,
            help="introspect server port")
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument("--file",
            help="where to look for snmp credentials")
        group.add_argument("--api_serever",
            help="ip:port of api-server for snmp credentials")
        group.add_argument("--discovery_serever",
            help="ip:port of dicovery to get api-sever snmp credentials")
        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if self._args.file:
            devcfg = ConfigParser.SafeConfigParser()
            devcfg.optionxform = str
            devcfg.read([self._args.file])
            for dev in devcfg.sections():
                nd = dict(devcfg.items(dev))
                mibs = self._mklist(self._get_and_remove_key(nd, 
                            'Mibs', []))
                flow_export_source_ip = self._get_and_remove_key(nd, 
                        'FlowExportSourceIp')
                self._devices.append(DeviceConfig(dev, nd, mibs,
                            flow_export_source_ip))
        self._args.config_sections = config

    def _get_and_remove_key(self, data_dict, key, default=None):
        val = default
        if key in data_dict:
            val = data_dict[key]
            del data_dict[key]
        return val



    def _pat(self):
        if self.__pat is None:
           self.__pat = re.compile(', *| +')
        return self.__pat

    def _mklist(self, s):
        if isinstance(s, str):
            return self._pat().split(s)
        return s

    def devices(self):
        for d in self._devices:
            yield d

    def discovery(self):
        return {'server':self._args.discovery_server,
            'port':self._args.discovery_port}

    def collectors(self):
        return self._args.collectors

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

