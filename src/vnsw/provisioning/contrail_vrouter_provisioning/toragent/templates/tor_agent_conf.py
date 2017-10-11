import string

template = string.Template("""

#
# Vnswad configuration options
#

[CONTROL-NODE]
# IP address to be used to connect to control-node. Maximum of 2 IP addresses
# (separated by a space) can be provided. If no IP is configured then the
# value provided by discovery service will be used. (optional)
servers=$__contrail_control_servers__

[DEFAULT]
agent_name=$__contrail_tor_agent_name__
# Everything in this section is optional

# IP address and port to be used to connect to collector. If these are not
# configured, value provided by discovery service will be used. Multiple
# IP:port strings separated by space can be provided
collectors=$__contrail_collector_servers__

# Enable/disable debug logging. Possible values are 0 (disable) and 1 (enable)
# debug=0

# Aging time for flow-records in seconds
# flow_cache_timeout=0

# Hostname of compute-node. If this is not configured value from `hostname`
# will be taken
# hostname=

# Category for logging. Default value is '*'
# log_category=

# Local log file name
log_file=/var/log/contrail/contrail-tor-agent-$__contrail_tor_id__.log

# Log severity levels. Possible values are SYS_EMERG, SYS_ALERT, SYS_CRIT,
# SYS_ERR, SYS_WARN, SYS_NOTICE, SYS_INFO and SYS_DEBUG. Default is SYS_DEBUG
# log_level=SYS_DEBUG

# Enable/Disable local file logging. Possible values are 0 (disable) and 1 (enable)
# log_local=0

# Enable/Disable local flow message logging. Possible values are 0 (disable) and 1 (enable)
# log_flow=0

# Encapsulation type for tunnel. Possible values are MPLSoGRE, MPLSoUDP, VXLAN
# tunnel_type=

# Enable/Disable headless mode for agent. In headless mode agent retains last
# known good configuration from control node when all control nodes are lost.
# Possible values are true(enable) and false(disable)
# headless_mode=

# Define agent mode. Only supported value is "tor"
  agent_mode=tor


# Http server port for inspecting vnswad state (useful for debugging)
# http_server_port=8085
http_server_port=$__contrail_http_server_port__

# Number of control-nodes info to be provided by Discovery service. Possible
# values are 1 and 2
# max_control_nodes=1

# Enable/Disable SSL based XMPP Authentication
xmpp_auth_enable=$__xmpp_auth_enable__
xmpp_dns_auth_enable=$__xmpp_dns_auth_enable__

[DNS]
# IP address to be used to connect to dns-node. Maximum of 2 IP addresses
# (separated by a space) can be provided. If no IP is configured then the
# value provided by discovery service will be used. (Optional)
servers=$__contrail_dns_servers__

[NETWORKS]
# control-channel IP address used by WEB-UI to connect to vnswad to fetch
# required information (Optional)
control_network_ip=$__contrail_control_ip__

[TOR]
# IP address of the TOR to manage
tor_ip=$__contrail_tor_ip__

# Identifier for ToR. Agent will subscribe to ifmap-configuration by this name
tor_id=$__contrail_tor_id__

# ToR management scheme is based on this type. Only supported value is "ovs"
tor_type=ovs

# OVS server port number on the ToR
tor_ovs_port=$__contrail_tsn_ovs_port__

# IP-Transport protocol used to connect to tor. Supported values are "tcp", "pssl"
tor_ovs_protocol=$__contrail_tor_ovs_protocol__

#tor name on the TOR
tor_name=$__contrail_tor_name__

# tor vendor name
tor_vendor_name=$__contrail_tor_vendor_name__

#tor product name
tor_product_name=$__contrail_tor_product_name__

# Path to ssl certificate for tor-agent, needed for pssl
ssl_cert=$__contrail_tor_ssl_cert__

# Path to ssl private-key for tor-agent, needed for pssl
ssl_privkey=$__contrail_tor_ssl_privkey__

# Path to ssl cacert for tor-agent, needed for pssl
ssl_cacert=$__contrail_tor_ssl_cacert__

tsn_ip=$__contrail_tsn_ip__

# OVS keep alive timer interval in milliseconds
tor_keepalive_interval=$__contrail_tor_agent_ovs_ka__
""")
