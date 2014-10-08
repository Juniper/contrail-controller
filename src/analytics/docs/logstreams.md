Real-time log streams
=====================

Configuration
-------------
The default directory for configuration files is /etc/contrail/logstreams,
but can be reconfigured by setting DEFAULT.streaming_server_config_dir in
/etc/contrail/contrail-collector.conf.

Currently the only available implementation sends unchanged Sandesh messages
via TCP/IP connection. There are plans for AVRO implementation.

To add another stream, create a config file in aforementioned directory and
provide fields listed below.

[stream]
unique_name = logstream
type = XMLOutputStream
dest_addr = 127.0.0.1
dest_port = 50000
reconnect_timeout_ms = 500


Development
-----------
The implementation was designed so that it would be easy to implement
configuration via IF-MAP listener later (see also:
"ifmap/ifmap_config_listener.h"). There is no support for dynamically
loadable plugins yet, but again, it can be implemented easily.
