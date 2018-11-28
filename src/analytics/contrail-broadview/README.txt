=====================
Contrail Broadview TEST
=====================

Contrail Broadview periodically polls switches with
broadview supported asics (Trident and Tomahawk) and collets 
data using the rest API and creates contrail uve

daemon runs on analytics node 

contrail-broadview -c /etc/contrail/contrail-broadview.conf 

Command Line
============
contrail-broadview [-h] [-c FILE]
                        [--analytics_api ANALYTICS_API [ANALYTICS_API ...]]
                        [--collectors COLLECTORS [COLLECTORS ...]]
                        [--log_file LOG_FILE] [--log_local]
                        [--log_category LOG_CATEGORY] [--log_level LOG_LEVEL]
                        [--use_syslog] [--syslog_facility SYSLOG_FACILITY]
                        [--scan_frequency SCAN_FREQUENCY]
                        [--http_server_port HTTP_SERVER_PORT]
                        [--api_serever API_SEREVER]

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
  --api_serever API_SEREVER
                        ip:port of api-server for snmp credentials

