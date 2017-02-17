=====================
Contrail SNMP collector
=====================

Contrail SNMP collector uses netsnmp library to
collect MIB data from the underlay switches &
routers. It pubishes these data in UVE, to be 
consumed by analytics application and GUI

Typically it is installed in the cron job with desired
frequency of update::

    contrail-snmp-collector -c /etc/contrail/contrail-snmp-collector.conf \
        --file /etc/contrail/snmp-devices.ini

Command Line
============

contrail-snmp-collector -h
usage: contrail-snmp-collector [-h] [-c FILE]
                             [--collectors COLLECTORS [COLLECTORS ...]]
                             [--log_file LOG_FILE] [--log_local]
                             [--log_category LOG_CATEGORY]
                             [--log_level LOG_LEVEL] [--use_syslog]
                             [--syslog_facility SYSLOG_FACILITY]
                             [--scan_frequency SCAN_FREQUENCY]
                             [--http_server_port HTTP_SERVER_PORT]
                             (--file FILE | --api_serever API_SEREVER )

optional arguments:
  -h, --help            show this help message and exit
  -c FILE, --conf_file FILE
                        Specify config file
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
  --file FILE           where to look for snmp credentials
  --api_serever API_SEREVER
                        ip:port of api-server for snmp credentials


Device File
===========

The device file is in ini format. 

* It provides the list of
  switches and their credentials to scan. 
ommunity = public
Version = 2
Mibs = LldpTable, ArpTable

[10.84.6.191]
Community = public
Version = 2


