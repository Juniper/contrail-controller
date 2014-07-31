/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#include "vr_defs.h"
#include "cmn/agent_cmn.h"
#include "oper/route_common.h"
#include "oper/vn.h"
#include "pkt/pkt_init.h"
#include "services/dhcp_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dns_proto.h"
#include "services/services_sandesh.h"
#include "bind/bind_util.h"
#include "bind/xmpp_dns_agent.h"

#include <boost/assign/list_of.hpp>
#include <bind/bind_util.h>

using namespace boost::assign;
Dhcpv4NameCodeMap g_dhcpv4_namecode_map =
    map_list_of<std::string, uint32_t>
        ("subnet-mask", DHCP_OPTION_SUBNET_MASK)
        ("time-offset", DHCP_OPTION_TIME_OFFSET)
        ("routers", DHCP_OPTION_ROUTER)
        ("time-servers", DHCP_OPTION_TIME_SERVER)
        ("name-servers", DHCP_OPTION_NAME_SERVER)
        ("domain-name-servers", DHCP_OPTION_DNS)
        ("log-servers", DHCP_OPTION_LOG_SERVER)
        ("quote-servers", DHCP_OPTION_QUOTE_SERVER)
        ("lpr-servers", DHCP_OPTION_LPR_SERVER)
        ("impress-servers", DHCP_OPTION_IMPRESS_SERVER)
        ("resource-location-servers", DHCP_OPTION_RESOURCE_LOCATION_SERVER)
        ("host-name", DHCP_OPTION_HOST_NAME)
        ("boot-size", DHCP_OPTION_BOOT_FILE_SIZE)
        ("merit-dump", DHCP_OPTION_MERIT_DUMP_FILE)
        ("domain-name", DHCP_OPTION_DOMAIN_NAME)
        ("swap-server", DHCP_OPTION_SWAP_SERVER)
        ("root-path", DHCP_OPTION_ROOT_PATH)
        ("extension-path", DHCP_OPTION_EXTENSION_PATH)
        ("ip-forwarding", DHCP_OPTION_IP_FWD_CONTROL)
        ("non-local-source-routing", DHCP_OPTION_NL_SRC_ROUTING)
        ("policy-filter", DHCP_OPTION_POLICY_FILTER)
        ("max-dgram-reassembly", DHCP_OPTION_MAX_DG_REASSEMBLY_SIZE)
        ("default-ip-ttl", DHCP_OPTION_DEFAULT_IP_TTL)
        ("path-mtu-aging-timeout", DHCP_OPTION_PATH_MTU_AGING_TIMEOUT)
        ("path-mtu-plateau-table", DHCP_OPTION_PATH_MTU_PLATEAU_TABLE)
        ("interface-mtu", DHCP_OPTION_INTERFACE_MTU)
        ("all-subnets-local", DHCP_OPTION_ALL_SUBNETS_LOCAL)
        ("broadcast-address", DHCP_OPTION_BCAST_ADDRESS)
        ("perform-mask-discovery", DHCP_OPTION_PERFORM_MASK_DISCOVERY)
        ("mask-supplier", DHCP_OPTION_MASK_SUPPLIER)
        ("router-discovery", DHCP_OPTION_PERFORM_ROUTER_DISCOVERY)
        ("router-solicitation-address", DHCP_OPTION_ROUTER_SOLICIT_ADDRESS)
        ("static-routes", DHCP_OPTION_STATIC_ROUTING_TABLE)
        ("trailer-encapsulation", DHCP_OPTION_TRAILER_ENCAP)
        ("arp-cache-timeout", DHCP_OPTION_ARP_CACHE_TIMEOUT)
        ("ieee802-3-encapsulation", DHCP_OPTION_ETHERNET_ENCAP)
        ("default-tcp-ttl", DHCP_OPTION_DEFAULT_TCP_TTL)
        ("tcp-keepalive-interval", DHCP_OPTION_TCP_KEEPALIVE_INTERVAL)
        ("tcp-keepalive-garbage", DHCP_OPTION_TCP_KEEPALIVE_GARBAGE)
        ("nis-domain", DHCP_OPTION_NIS_DOMAIN)
        ("nis-servers", DHCP_OPTION_NIS_SERVERS)
        ("ntp-servers", DHCP_OPTION_NTP_SERVERS)
        ("vendor-encapsulated-options", DHCP_OPTION_VENDOR_SPECIFIC_INFO)
        ("netbios-name-servers", DHCP_OPTION_NETBIOS_OVER_TCP_NS)
        ("netbios-dd-server", DHCP_OPTION_NETBIOS_OVER_TCP_DG_DS)
        ("netbios-node-type", DHCP_OPTION_NETBIOS_OVER_TCP_NODE_TYPE)
        ("netbios-scope", DHCP_OPTION_NETBIOS_OVER_TCP_SCOPE)
        ("font-servers", DHCP_OPTION_XWINDOW_FONT_SERVER)
        ("x-display-manager", DHCP_OPTION_XWINDOW_SYSTEM_DISP_MGR)
        ("dhcp-requested-address", DHCP_OPTION_REQ_IP_ADDRESS)
        ("dhcp-lease-time", DHCP_OPTION_IP_LEASE_TIME)
        ("dhcp-option-overload", DHCP_OPTION_OVERLOAD)
        ("dhcp-message-type", DHCP_OPTION_MSG_TYPE)
        ("dhcp-server-identifier", DHCP_OPTION_SERVER_IDENTIFIER)
        ("dhcp-parameter-request-list", DHCP_OPTION_PARAMETER_REQUEST_LIST)
        ("dhcp-message", DHCP_OPTION_MESSAGE)
        ("dhcp-max-message-size", DHCP_OPTION_MAX_DHCP_MSG_SIZE)
        ("dhcp-renewal-time", DHCP_OPTION_RENEW_TIME_VALUE)
        ("dhcp-rebinding-time", DHCP_OPTION_REBIND_TIME_VALUE)
        ("class-id", DHCP_OPTION_CLASS_ID)
        ("dhcp-client-identifier", DHCP_OPTION_CLIENT_ID)
        ("nwip-domain", DHCP_OPTION_NETWARE_IP_DOMAIN_NAME)
        ("nwip-suboptions", DHCP_OPTION_NETWARE_IP_INFO)
        ("nisplus-domain", DHCP_OPTION_NIS_PLUS_DOMAIN)
        ("nisplus-servers", DHCP_OPTION_NIS_PLUS_SERVERS)
        ("tftp-server-name", DHCP_OPTION_TFTP_SERVER_NAME)
        ("bootfile-name", DHCP_OPTION_BOOTFILE_NAME)
        ("mobile-ip-home-agent", DHCP_OPTION_MOBILE_IP_HA)
        ("smtp-server", DHCP_OPTION_SMTP_SERVER)
        ("pop-server", DHCP_OPTION_POP_SERVER)
        ("nntp-server", DHCP_OPTION_NNTP_SERVER)
        ("www-server", DHCP_OPTION_DEFAULT_WWW_SERVER)
        ("finger-server", DHCP_OPTION_DEFAULT_FINGER_SERVER)
        ("irc-server", DHCP_OPTION_DEFAULT_IRC_SERVER)
        ("streettalk-server", DHCP_OPTION_STREETTALK_SERVER)
        ("streettalk-directory-assistance-server", DHCP_OPTION_STREETTALK_DA_SERVER)
        ("user-class", DHCP_OPTION_USER_CLASS_INFO)
        ("slp-directory-agent", DHCP_OPTION_SLP_DIRECTORY_AGENT)
        ("slp-service-scope", DHCP_OPTION_SLP_SERVICE_SCOPE)
        ("rapid-commit", DHCP_OPTION_RAPID_COMMIT)
        ("client-fqdn", DHCP_OPTION_CLIENT_FQDN)
        ("storage-ns", DHCP_OPTION_STORAGE_NS)
        // option 82 not required
        ("nds-servers", DHCP_OPTION_NDS_SERVERS)
        ("nds-tree-name", DHCP_OPTION_NDS_TREE_NAME)
        ("nds-context", DHCP_OPTION_NDS_CONTEXT)
        ("bcms-controller-names", DHCP_OPTION_BCMCS_DN_LIST)
        ("bcms-controller-address", DHCP_OPTION_BCMCS_ADDR_LIST)
        ("dhcp-auth", DHCP_OPTION_AUTH)
        ("dhcp-client-last-time", DHCP_OPTION_CLIENT_LAST_XTIME)
        ("associated-ip", DHCP_OPTION_ASSOCIATE_IP)
        ("system-architecture", DHCP_OPTION_CLIENT_SYSARCH_TYPE)
        ("interface-id", DHCP_OPTION_CLIENT_NW_INTERFACE_ID)
        ("ldap-servers", DHCP_OPTION_LDAP)
        ("machine-id", DHCP_OPTION_CLIENT_MACHINE_ID)
        ("user-auth", DHCP_OPTION_OPENGROUP_USER_AUTH)
        ("geoconf-civic", DHCP_OPTION_GEOCONF_CIVIC)
        ("ieee-1003-1-tz", DHCP_OPTION_IEEE_1003_1_TZ)
        ("ref-tz-db", DHCP_OPTION_REF_TZ_DB)
        ("netinfo-server-address", DHCP_OPTION_NETINFO_PARENT_SERVER_ADDR)
        ("netinfo-server-tag", DHCP_OPTION_NETINFO_PARENT_SERVER_TAG)
        ("default-url", DHCP_OPTION_URL)
        ("auto-configure", DHCP_OPTION_AUTO_CONFIGURE)
        ("name-search", DHCP_OPTION_NAME_SERVICE_SEARCH)
        ("subnet-selection", DHCP_OPTION_SUBNET_SELECTION)
        ("domain-search", DHCP_OPTION_DNS_DOMAIN_SEARCH_LIST)
        ("sip-servers", DHCP_OPTION_SIP_SERVERS)
        ("classless-static-routes", DHCP_OPTION_CLASSLESS_ROUTE)
        ("dhcp-ccc", DHCP_OPTION_CCC)
        ("dhcp-geoconf", DHCP_OPTION_GEOCONF)
        ("vendor-class-identifier", DHCP_OPTION_VENDOR_ID_VENDOR_CLASS)
        ("vivso", DHCP_OPTION_VENDOR_ID_VENDOR_SPECIFIC)
        ("tftp-server", DHCP_OPTION_TFTP_SERVER)
        ("pxe-vendor-specific-129", DHCP_OPTION_PXE_VENDOR_SPECIFIC_129)
        ("pxe-vendor-specific-130", DHCP_OPTION_PXE_VENDOR_SPECIFIC_130)
        ("pxe-vendor-specific-131", DHCP_OPTION_PXE_VENDOR_SPECIFIC_131)
        ("pxe-vendor-specific-132", DHCP_OPTION_PXE_VENDOR_SPECIFIC_132)
        ("pxe-vendor-specific-133", DHCP_OPTION_PXE_VENDOR_SPECIFIC_133)
        ("pxe-vendor-specific-134", DHCP_OPTION_PXE_VENDOR_SPECIFIC_134)
        ("pxe-vendor-specific-135", DHCP_OPTION_PXE_VENDOR_SPECIFIC_135)
        ("pana-agent", DHCP_OPTION_PANA_AUTH_AGENT)
        ("lost-server", DHCP_OPTION_LOST_SERVER)
        ("capwap-ac-v4", DHCP_OPTION_CAPWAP_AC_ADDRESS)
        ("dhcp-mos", DHCP_OPTION_IPV4_ADDRESS_MOS)
        ("dhcp-fqdn-mos", DHCP_OPTION_IPV4_FQDN_MOS)
        ("sip-ua-config-domain", DHCP_OPTION_SIP_UA_CONFIG_DOMAIN)
        ("andsf-servers", DHCP_OPTION_IPV4_ADDRESS_ANDSF)
        ("dhcp-geoloc", DHCP_OPTION_GEOLOC)
        ("force-renew-nonce-cap", DHCP_OPTION_FORCERENEW_NONCE_CAP)
        ("rdnss-selection", DHCP_OPTION_RDNSS_SELECTION)
        ("tftp-server-address", DHCP_OPTION_TFTP_SERVER_ADDRESS)
        ("status-code", DHCP_OPTION_STATUS_CODE)
        ("dhcp-base-time", DHCP_OPTION_BASE_TIME)
        ("dhcp-state-start-time", DHCP_OPTION_START_TIME_OF_STATE)
        ("dhcp-query-start-time", DHCP_OPTION_QUERY_START_TIME)
        ("dhcp-query-end-time", DHCP_OPTION_QUERY_END_TIME)
        ("dhcp-state", DHCP_OPTION_DHCP_STATE)
        ("data-source", DHCP_OPTION_DATA_SOURCE)
        ("pcp-server", DHCP_OPTION_PCP_SERVER)
        ("dhcp-pxe-magic", DHCP_OPTION_PXELINUX_MAGIC)
        ("config-file", DHCP_OPTION_CONFIG_FILE)
        ("path-prefix", DHCP_OPTION_PATH_PREFIX)
        ("reboot-time", DHCP_OPTION_REBOOT_TIME)
        ("dhcp-6rd", DHCP_OPTION_6RD)
        ("dhcp-access-domain", DHCP_OPTION_V4_ACCESS_DOMAIN)
        ("subnet-allocation", DHCP_OPTION_SUBNET_ALLOCATION)
        ("dhcp-vss", DHCP_OPTION_VSS);

Dhcpv4CategoryMap g_dhcpv4_category_map =
    map_list_of<uint32_t, DhcpHandler::DhcpOptionCategory>
        // (DHCP_OPTION_SUBNET_MASK, DhcpHandler::OneIP)       // agent adds this option
        (DHCP_OPTION_TIME_OFFSET, DhcpHandler::Int32bit)
        (DHCP_OPTION_ROUTER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_TIME_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_NAME_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_DNS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_LOG_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_QUOTE_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_LPR_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_IMPRESS_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_RESOURCE_LOCATION_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_HOST_NAME, DhcpHandler::String)
        (DHCP_OPTION_BOOT_FILE_SIZE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_MERIT_DUMP_FILE, DhcpHandler::String)
        (DHCP_OPTION_DOMAIN_NAME, DhcpHandler::String)
        (DHCP_OPTION_SWAP_SERVER, DhcpHandler::OneIP)
        (DHCP_OPTION_ROOT_PATH, DhcpHandler::String)
        (DHCP_OPTION_EXTENSION_PATH, DhcpHandler::String)
        (DHCP_OPTION_IP_FWD_CONTROL, DhcpHandler::Bool)
        (DHCP_OPTION_NL_SRC_ROUTING, DhcpHandler::Bool)
        (DHCP_OPTION_POLICY_FILTER, DhcpHandler::TwoIPPlus)
        (DHCP_OPTION_MAX_DG_REASSEMBLY_SIZE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_DEFAULT_IP_TTL, DhcpHandler::Byte)
        (DHCP_OPTION_PATH_MTU_AGING_TIMEOUT, DhcpHandler::Uint32bit)
        (DHCP_OPTION_PATH_MTU_PLATEAU_TABLE, DhcpHandler::Uint16bitArray)
        (DHCP_OPTION_INTERFACE_MTU, DhcpHandler::Uint16bit)
        (DHCP_OPTION_ALL_SUBNETS_LOCAL, DhcpHandler::Bool)
        // (DHCP_OPTION_BCAST_ADDRESS, DhcpHandler::OneIP)     // agent adds this option
        (DHCP_OPTION_PERFORM_MASK_DISCOVERY, DhcpHandler::Bool)
        (DHCP_OPTION_MASK_SUPPLIER, DhcpHandler::Bool)
        (DHCP_OPTION_PERFORM_ROUTER_DISCOVERY, DhcpHandler::Bool)
        (DHCP_OPTION_ROUTER_SOLICIT_ADDRESS, DhcpHandler::OneIP)
        (DHCP_OPTION_STATIC_ROUTING_TABLE, DhcpHandler::TwoIPPlus)
        (DHCP_OPTION_TRAILER_ENCAP, DhcpHandler::Bool)
        (DHCP_OPTION_ARP_CACHE_TIMEOUT, DhcpHandler::Uint32bit)
        (DHCP_OPTION_ETHERNET_ENCAP, DhcpHandler::Bool)
        (DHCP_OPTION_DEFAULT_TCP_TTL, DhcpHandler::Byte)
        (DHCP_OPTION_TCP_KEEPALIVE_INTERVAL, DhcpHandler::Uint32bit)
        (DHCP_OPTION_TCP_KEEPALIVE_GARBAGE, DhcpHandler::Bool)
        (DHCP_OPTION_NIS_DOMAIN, DhcpHandler::String)
        (DHCP_OPTION_NIS_SERVERS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_NTP_SERVERS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_VENDOR_SPECIFIC_INFO, DhcpHandler::String)
        (DHCP_OPTION_NETBIOS_OVER_TCP_NS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_NETBIOS_OVER_TCP_DG_DS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_NETBIOS_OVER_TCP_NODE_TYPE, DhcpHandler::Byte)
        (DHCP_OPTION_NETBIOS_OVER_TCP_SCOPE, DhcpHandler::String)
        (DHCP_OPTION_XWINDOW_FONT_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_XWINDOW_SYSTEM_DISP_MGR, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_REQ_IP_ADDRESS, DhcpHandler::OneIP)
        // (DHCP_OPTION_IP_LEASE_TIME, DhcpHandler::Uint32bit) // agent adds this option
        (DHCP_OPTION_OVERLOAD, DhcpHandler::Byte)
        // (DHCP_OPTION_MSG_TYPE, DhcpHandler::Byte)           // agent adds this option
        // (DHCP_OPTION_SERVER_IDENTIFIER, DhcpHandler::OneIP) // agent adds this option
        (DHCP_OPTION_PARAMETER_REQUEST_LIST, DhcpHandler::ByteArray)
        (DHCP_OPTION_MESSAGE, DhcpHandler::String)
        (DHCP_OPTION_MAX_DHCP_MSG_SIZE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_RENEW_TIME_VALUE, DhcpHandler::Uint32bit)
        (DHCP_OPTION_REBIND_TIME_VALUE, DhcpHandler::Uint32bit)
        (DHCP_OPTION_CLASS_ID, DhcpHandler::String)
        (DHCP_OPTION_CLIENT_ID, DhcpHandler::ByteString)
        (DHCP_OPTION_NETWARE_IP_DOMAIN_NAME, DhcpHandler::String)
        (DHCP_OPTION_NETWARE_IP_INFO, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_NIS_PLUS_DOMAIN, DhcpHandler::String)
        (DHCP_OPTION_NIS_PLUS_SERVERS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_TFTP_SERVER_NAME, DhcpHandler::String)
        (DHCP_OPTION_BOOTFILE_NAME, DhcpHandler::String)
        (DHCP_OPTION_MOBILE_IP_HA, DhcpHandler::ZeroIPPlus)
        (DHCP_OPTION_SMTP_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_POP_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_NNTP_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_DEFAULT_WWW_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_DEFAULT_FINGER_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_DEFAULT_IRC_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_STREETTALK_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_STREETTALK_DA_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_USER_CLASS_INFO, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_SLP_DIRECTORY_AGENT, DhcpHandler::ByteOneIPPlus)
        (DHCP_OPTION_SLP_SERVICE_SCOPE, DhcpHandler::ByteString)
        (DHCP_OPTION_RAPID_COMMIT, DhcpHandler::NoData)
        // (DHCP_OPTION_CLIENT_FQDN, DhcpHandler::ByteArray)   // sent by clients
        (DHCP_OPTION_STORAGE_NS, DhcpHandler::ByteArray)       // send encoded data
        (DHCP_OPTION_NDS_SERVERS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_NDS_TREE_NAME, DhcpHandler::ByteArray)    // send encoded data
        (DHCP_OPTION_NDS_CONTEXT, DhcpHandler::ByteArray)      // send encoded data
        (DHCP_OPTION_BCMCS_DN_LIST, DhcpHandler::ByteArray)    // send encoded data
        (DHCP_OPTION_BCMCS_ADDR_LIST, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_AUTH, DhcpHandler::ByteArray)             // send encoded data
        (DHCP_OPTION_CLIENT_LAST_XTIME, DhcpHandler::Uint32bit)
        (DHCP_OPTION_ASSOCIATE_IP, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_CLIENT_SYSARCH_TYPE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_CLIENT_NW_INTERFACE_ID, DhcpHandler::ByteArray)
        (DHCP_OPTION_LDAP, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_CLIENT_MACHINE_ID, DhcpHandler::String)
        (DHCP_OPTION_OPENGROUP_USER_AUTH, DhcpHandler::String)
        (DHCP_OPTION_GEOCONF_CIVIC, DhcpHandler::ByteArray)    // send encoded data
        (DHCP_OPTION_IEEE_1003_1_TZ, DhcpHandler::String)
        (DHCP_OPTION_REF_TZ_DB, DhcpHandler::String)
        (DHCP_OPTION_NETINFO_PARENT_SERVER_TAG, DhcpHandler::String)
        (DHCP_OPTION_NETINFO_PARENT_SERVER_ADDR, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_URL, DhcpHandler::String)
        (DHCP_OPTION_AUTO_CONFIGURE, DhcpHandler::Bool)
        (DHCP_OPTION_NAME_SERVICE_SEARCH, DhcpHandler::Uint16bitArray)
        (DHCP_OPTION_SUBNET_SELECTION, DhcpHandler::OneIP)
        (DHCP_OPTION_DNS_DOMAIN_SEARCH_LIST, DhcpHandler::NameCompression)
        (DHCP_OPTION_SIP_SERVERS, DhcpHandler::ByteArray)                // send encoded data
        (DHCP_OPTION_CLASSLESS_ROUTE, DhcpHandler::ClasslessRoute)
        (DHCP_OPTION_CCC, DhcpHandler::ByteArray)                        // send encoded data
        (DHCP_OPTION_GEOCONF, DhcpHandler::ByteArray)                    // send encoded data
        (DHCP_OPTION_VENDOR_ID_VENDOR_CLASS, DhcpHandler::ByteArray)     // send encoded data
        (DHCP_OPTION_VENDOR_ID_VENDOR_SPECIFIC, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_TFTP_SERVER, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_129, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_130, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_131, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_132, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_133, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_134, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_135, DhcpHandler::String)
        (DHCP_OPTION_PANA_AUTH_AGENT, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_LOST_SERVER, DhcpHandler::ByteArray)         // send encoded data
        (DHCP_OPTION_CAPWAP_AC_ADDRESS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_IPV4_ADDRESS_MOS, DhcpHandler::ByteArray)     // send encoded data
        (DHCP_OPTION_IPV4_FQDN_MOS, DhcpHandler::ByteArray)        // send encoded data
        (DHCP_OPTION_SIP_UA_CONFIG_DOMAIN, DhcpHandler::ByteArray) // send encoded data
        (DHCP_OPTION_IPV4_ADDRESS_ANDSF, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_GEOLOC, DhcpHandler::ByteArray)               // send encoded data
        (DHCP_OPTION_FORCERENEW_NONCE_CAP, DhcpHandler::ByteArray) // send encoded data
        (DHCP_OPTION_RDNSS_SELECTION, DhcpHandler::ByteArray)      // send encoded data
        (DHCP_OPTION_TFTP_SERVER_ADDRESS, DhcpHandler::OneIPPlus)
        (DHCP_OPTION_STATUS_CODE, DhcpHandler::ByteString)
        (DHCP_OPTION_BASE_TIME, DhcpHandler::Uint32bit)
        (DHCP_OPTION_START_TIME_OF_STATE, DhcpHandler::Uint32bit)
        (DHCP_OPTION_QUERY_START_TIME, DhcpHandler::Uint32bit)
        (DHCP_OPTION_QUERY_END_TIME, DhcpHandler::Uint32bit)
        (DHCP_OPTION_DHCP_STATE, DhcpHandler::Byte)
        (DHCP_OPTION_DATA_SOURCE, DhcpHandler::Byte)
        (DHCP_OPTION_PCP_SERVER, DhcpHandler::ByteArray)      // send encoded data
        (DHCP_OPTION_PXELINUX_MAGIC, DhcpHandler::Uint32bit)
        (DHCP_OPTION_CONFIG_FILE, DhcpHandler::String)
        (DHCP_OPTION_PATH_PREFIX, DhcpHandler::String)
        (DHCP_OPTION_REBOOT_TIME, DhcpHandler::Uint32bit)
        (DHCP_OPTION_6RD, DhcpHandler::ByteArray)               // send encoded data
        (DHCP_OPTION_SUBNET_ALLOCATION, DhcpHandler::ByteArray)
        (DHCP_OPTION_V4_ACCESS_DOMAIN, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_VSS, DhcpHandler::ByteString)
        (DHCP_OPTION_PAD, DhcpHandler::None)
        (DHCP_OPTION_END, DhcpHandler::None);

DhcpHandler::DhcpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                         boost::asio::io_service &io)
        : ProtoHandler(agent, info, io), vm_itf_(NULL), vm_itf_index_(-1),
          msg_type_(DHCP_UNKNOWN), out_msg_type_(DHCP_UNKNOWN), flags_(),
          host_routes_level_(Invalid),
          nak_msg_("cannot assign requested address") {
    ipam_type_.ipam_dns_method = "none";
};

bool DhcpHandler::Run() {
    switch(pkt_info_->type) {
        case PktType::MESSAGE:
            return HandleMessage();

       default:
            return HandleVmRequest();
    }
}

bool DhcpHandler::HandleVmRequest() {
    dhcp_ = (dhcphdr *) pkt_info_->data;
    request_.UpdateData(dhcp_->xid, ntohs(dhcp_->flags), dhcp_->chaddr);
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        dhcp_proto->IncrStatsOther();
        DHCP_TRACE(Error, "Received DHCP packet on invalid interface : "
                   << GetInterfaceIndex());
        return true;
    }

    if (itf->type() != Interface::VM_INTERFACE) {
        dhcp_proto->IncrStatsErrors();
        DHCP_TRACE(Error, "Received DHCP packet on non VM port interface : "
                   << GetInterfaceIndex());
        return true;
    }
    vm_itf_ = static_cast<VmInterface *>(itf);
    if (!vm_itf_->ipv4_forwarding()) {
        DHCP_TRACE(Error, "DHCP request on VM port with disabled ipv4 service: "
                   << GetInterfaceIndex());
        return true;
    }

    // For VM interfaces in default VRF, if the config doesnt have IP address,
    // send the request to fabric
    if (vm_itf_->vrf() && vm_itf_->do_dhcp_relay()) {
        RelayRequestToFabric();
        return true;
    }

    // options length = pkt length - size of headers
    int16_t options_len = pkt_info_->len - EncapHeaderLen() -
                            sizeof(struct ether_header) -
                            sizeof(struct ip) - sizeof(struct udphdr) -
                            DHCP_FIXED_LEN;
    if (!ReadOptions(options_len))
        return true;

    switch (msg_type_) {
        case DHCP_DISCOVER:
            out_msg_type_ = DHCP_OFFER;
            dhcp_proto->IncrStatsDiscover();
            DHCP_TRACE(Trace, "DHCP discover received on interface : "
                       << vm_itf_->name());
            break;

        case DHCP_REQUEST:
            out_msg_type_ = DHCP_ACK;
            dhcp_proto->IncrStatsRequest();
            DHCP_TRACE(Trace, "DHCP request received on interface : "
                       << vm_itf_->name());
            break;

        case DHCP_INFORM:
            out_msg_type_ = DHCP_ACK;
            dhcp_proto->IncrStatsInform();
            DHCP_TRACE(Trace, "DHCP inform received on interface : "
                       << vm_itf_->name());
            break;

        case DHCP_DECLINE:
            dhcp_proto->IncrStatsDecline();
            DHCP_TRACE(Error, "DHCP Client declined the offer : vrf = " <<
                       pkt_info_->vrf << " ifindex = " << GetInterfaceIndex());
            return true;

        case DHCP_ACK:
        case DHCP_NAK:
        case DHCP_RELEASE:
        case DHCP_LEASE_QUERY:
        case DHCP_LEASE_UNASSIGNED:
        case DHCP_LEASE_UNKNOWN:
        case DHCP_LEASE_ACTIVE:
        default:
            DHCP_TRACE(Trace, ServicesSandesh::DhcpMsgType(msg_type_) <<
                       " received on interface : " << vm_itf_->name() <<
                       "; ignoring");
            dhcp_proto->IncrStatsOther();
            return true;
    }

    if (FindLeaseData()) {
        UpdateDnsServer();
        SendDhcpResponse();
        Ip4Address ip(config_.ip_addr);
        DHCP_TRACE(Trace, "DHCP response sent; message = " <<
                   ServicesSandesh::DhcpMsgType(out_msg_type_) <<
                   "; ip = " << ip.to_string());
    }

    return true;
}

bool DhcpHandler::HandleMessage() {
    switch (pkt_info_->ipc->cmd) {
        case DhcpProto::DHCP_VHOST_MSG:
            // DHCP message from DHCP server port that we listen on
            return HandleDhcpFromFabric();

        default:
            assert(0);
    }
}

// Handle any DHCP response coming from fabric for a request that we relayed
bool DhcpHandler::HandleDhcpFromFabric() {
    DhcpProto::DhcpVhostMsg *ipc = static_cast<DhcpProto::DhcpVhostMsg *>(pkt_info_->ipc);
    pkt_info_->len = ipc->len;
    dhcp_ = reinterpret_cast<dhcphdr *>(ipc->pkt);
    request_.UpdateData(dhcp_->xid, ntohs(dhcp_->flags), dhcp_->chaddr);

    int16_t options_len = ipc->len - DHCP_FIXED_LEN;
    if (ReadOptions(options_len) && vm_itf_ &&
        agent()->interface_table()->FindInterface(vm_itf_index_) == vm_itf_) {
        // this is a DHCP relay response for our request
        RelayResponseFromFabric();
    }

    delete ipc;
    return true;
}

// read DHCP options in the incoming packet
bool DhcpHandler::ReadOptions(int16_t opt_rem_len) {
    // verify magic cookie
    if ((opt_rem_len < 4) ||
        memcmp(dhcp_->options, DHCP_OPTIONS_COOKIE, 4)) {
        agent()->GetDhcpProto()->IncrStatsErrors();
        DHCP_TRACE(Error, "DHCP options cookie missing; vrf = " <<
                   pkt_info_->vrf << " ifindex = " << GetInterfaceIndex());
        return false;
    }

    opt_rem_len -= 4;
    DhcpOptions *opt = (DhcpOptions *)(dhcp_->options + 4);
    // parse thru the option fields
    while ((opt_rem_len > 0) && (opt->code != DHCP_OPTION_END)) {
        switch (opt->code) {
            case DHCP_OPTION_PAD:
                opt_rem_len -= 1;
                opt = (DhcpOptions *)((uint8_t *)opt + 1);
                continue;

            case DHCP_OPTION_MSG_TYPE:
                if (opt_rem_len >= opt->len + 2)
                    msg_type_ = *(uint8_t *)opt->data;
                break;

            case DHCP_OPTION_REQ_IP_ADDRESS:
                if (opt_rem_len >= opt->len + 2) {
                    union {
                        uint8_t data[sizeof(in_addr_t)];
                        in_addr_t addr;
                    } bytes;
                    memcpy(bytes.data, opt->data, sizeof(in_addr_t));
                    request_.ip_addr = ntohl(bytes.addr);
                }
                break;

            case DHCP_OPTION_HOST_NAME:
                if (opt_rem_len >= opt->len + 2)
                    config_.client_name_.assign((char *)opt->data, opt->len);
                break;

            case DHCP_OPTION_DOMAIN_NAME:
                if (opt_rem_len >= opt->len + 2)
                    config_.domain_name_.assign((char *)opt->data, opt->len);
                break;

            case DHCP_OPTION_82:
                ReadOption82(opt);
                break;

            default:
                break;
        }
        opt_rem_len -= (2 + opt->len);
        opt = (DhcpOptions *)((uint8_t *)opt + 2 + opt->len);
    }

    return true;
}

void DhcpHandler::FillDhcpInfo(uint32_t addr, int plen, uint32_t gw, uint32_t dns) {
    config_.ip_addr = addr;
    config_.plen = plen;
    uint32_t mask = plen? (0xFFFFFFFF << (32 - plen)) : 0;
    config_.subnet_mask = mask;
    config_.bcast_addr = (addr & mask) | ~mask;
    config_.gw_addr = gw;
    config_.dns_addr = dns;
}


bool DhcpHandler::FindLeaseData() {
    Ip4Address ip = vm_itf_->ip_addr();
    // Change client name to VM name; this is the name assigned to the VM
    config_.client_name_ = vm_itf_->vm_name();
    if (vm_itf_->ipv4_active()) {
        if (vm_itf_->fabric_port()) {
            Inet4UnicastRouteEntry *rt =
                Inet4UnicastAgentRouteTable::FindResolveRoute(
                             vm_itf_->vrf()->GetName(), ip);
            if (rt) {
                uint32_t gw = agent()->vhost_default_gateway().to_ulong();
                boost::system::error_code ec;
                if (IsIp4SubnetMember(rt->addr(),
                    Ip4Address::from_string("169.254.0.0", ec), rt->plen()))
                    gw = 0;
                FillDhcpInfo(ip.to_ulong(), rt->plen(), gw, gw);
                return true;
            }
            agent()->GetDhcpProto()->IncrStatsErrors();
            DHCP_TRACE(Error, "DHCP fabric port request failed : "
                       "could not find route for " << ip.to_string());
            return false;
        }

        const std::vector<VnIpam> &ipam = vm_itf_->vn()->GetVnIpam();
        unsigned int i;
        for (i = 0; i < ipam.size(); ++i) {
            if (IsIp4SubnetMember(ip, ipam[i].ip_prefix, ipam[i].plen)) {
                uint32_t default_gw = ipam[i].default_gw.to_ulong();
                FillDhcpInfo(ip.to_ulong(), ipam[i].plen, default_gw, default_gw);
                return true;
            }
        }
    }

    // We dont have the config yet; give a short lease
    config_.lease_time = DHCP_SHORTLEASE_TIME;
    if (ip.to_ulong()) {
        // Give address received from Nova
        FillDhcpInfo(ip.to_ulong(), 32, 0, 0);
    } else {
        // Give a link local address
        boost::system::error_code ec;
        uint32_t gwip = Ip4Address::from_string(LINK_LOCAL_GW, ec).to_ulong();
        FillDhcpInfo((gwip & 0xFFFF0000) | (vm_itf_->id() & 0xFF),
                     16, 0, 0);
    }
    return true;
}

void DhcpHandler::UpdateDnsServer() {
    if (config_.lease_time != (uint32_t) -1)
        return;

    if (!vm_itf_->vn() ||
        !vm_itf_->vn()->GetIpamData(vm_itf_->ip_addr(), &ipam_name_,
                                    &ipam_type_)) {
        DHCP_TRACE(Trace, "Ipam data not found; VM = " << vm_itf_->name());
        return;
    }

    if (ipam_type_.ipam_dns_method != "virtual-dns-server" ||
        !agent()->domain_config_table()->GetVDns(ipam_type_.ipam_dns_server.
                                          virtual_dns_server_name, &vdns_type_))
        return;

    if (config_.domain_name_.size() &&
        config_.domain_name_ != vdns_type_.domain_name) {
        DHCP_TRACE(Trace, "Client domain " << config_.domain_name_ <<
                   " doesnt match with configured domain " <<
                   vdns_type_.domain_name << "; Client name = " <<
                   config_.client_name_);
    }
    std::size_t pos;
    if (config_.client_name_.size() &&
        ((pos = config_.client_name_.find('.', 0)) != std::string::npos) &&
        (config_.client_name_.substr(pos + 1) != vdns_type_.domain_name)) {
        DHCP_TRACE(Trace, "Client domain doesnt match with configured domain "
                   << vdns_type_.domain_name << "; Client name = "
                   << config_.client_name_);
        config_.client_name_.replace(config_.client_name_.begin() + pos + 1,
                                     config_.client_name_.end(),
                                     vdns_type_.domain_name);
    }
    config_.domain_name_ = vdns_type_.domain_name;

    if (out_msg_type_ != DHCP_ACK)
        return;

    agent()->GetDnsProto()->SendUpdateDnsEntry(
        vm_itf_, config_.client_name_, vm_itf_->ip_addr(), config_.plen,
        ipam_type_.ipam_dns_server.virtual_dns_server_name, vdns_type_,
        false, false);
}

void DhcpHandler::WriteOption82(DhcpOptions *opt, uint16_t *optlen) {
    optlen += 2;
    opt->code = DHCP_OPTION_82;
    opt->len = sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2;
    DhcpOptions *subopt = reinterpret_cast<DhcpOptions *>(opt->data);
    subopt->WriteWord(DHCP_SUBOP_CKTID, vm_itf_->id(), optlen);
    subopt = subopt->GetNextOptionPtr();
    subopt->WriteData(DHCP_SUBOP_REMOTEID, sizeof(VmInterface *),
                      &vm_itf_, optlen);
}

bool DhcpHandler::ReadOption82(DhcpOptions *opt) {
    if (opt->len != sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2)
        return false;

    DhcpOptions *subopt = reinterpret_cast<DhcpOptions *>(opt->data);
    for (int i = 0; i < 2; i++) {
        switch (subopt->code) {
            case DHCP_SUBOP_CKTID:
                if (subopt->len != sizeof(uint32_t))
                    return false;
                union {
                    uint8_t data[sizeof(uint32_t)];
                    uint32_t index;
                } bytes;

                memcpy(bytes.data, subopt->data, sizeof(uint32_t));
                vm_itf_index_ = ntohl(bytes.index);
                break;

            case DHCP_SUBOP_REMOTEID:
                if (subopt->len != sizeof(VmInterface *))
                    return false;
                memcpy(&vm_itf_, subopt->data, subopt->len);
                break;

            default:
                return false;
        }
        subopt = subopt->GetNextOptionPtr();
    }

    return true;
}

bool DhcpHandler::CreateRelayPacket() {
    PktInfo in_pkt_info = *pkt_info_.get();
    pkt_info_->pkt = new uint8_t[DHCP_PKT_SIZE];
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = in_pkt_info.vrf;
    pkt_info_->eth = (ether_header *)(pkt_info_->pkt + sizeof(ether_header) +
            sizeof(agent_hdr));
    pkt_info_->ip = (ip *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - EncapHeaderLen() - sizeof(ether_header)
                          - sizeof(ip) - sizeof(udphdr) - DHCP_FIXED_LEN - 4;
    uint16_t opt_len = 4;
    DhcpOptions *read_opt = (DhcpOptions *)(dhcp_->options + 4);
    DhcpOptions *write_opt = (DhcpOptions *)(dhcp->options + 4);
    while ((opt_rem_len > 0) && (read_opt->code != DHCP_OPTION_END)) {
        switch (read_opt->code) {
            case DHCP_OPTION_PAD:
                write_opt->WriteByte(DHCP_OPTION_PAD, &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                opt_rem_len -= 1;
                read_opt = read_opt->GetNextOptionPtr();
                continue;

            case DHCP_OPTION_82:
                break;

            case DHCP_OPTION_MSG_TYPE:
                msg_type_ = *(uint8_t *)read_opt->data;
                write_opt->WriteData(read_opt->code, read_opt->len, &msg_type_, &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            case DHCP_OPTION_HOST_NAME:
                config_.client_name_ = vm_itf_->vm_name();
                write_opt->WriteData(DHCP_OPTION_HOST_NAME,
                                     config_.client_name_.size(),
                                     config_.client_name_.c_str(), &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            default:
                write_opt->WriteData(read_opt->code, read_opt->len, &read_opt->data, &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;
        }
        opt_rem_len -= (2 + read_opt->len);
        read_opt = read_opt->GetNextOptionPtr();
    }
    dhcp_ = dhcp;
    dhcp->giaddr = htonl(agent()->router_id().to_ulong());
    WriteOption82(write_opt, &opt_len);
    write_opt = write_opt->GetNextOptionPtr();
    pkt_info_->sport = DHCP_SERVER_PORT;
    pkt_info_->dport = DHCP_SERVER_PORT;
    write_opt->WriteByte(DHCP_OPTION_END, &opt_len);
    pkt_info_->len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(pkt_info_->len, in_pkt_info.ip->ip_src.s_addr, pkt_info_->sport,
           in_pkt_info.ip->ip_dst.s_addr, pkt_info_->dport);
    pkt_info_->len += sizeof(ip);

    IpHdr(pkt_info_->len, htonl(agent()->router_id().to_ulong()),
          0xFFFFFFFF, IPPROTO_UDP);
    EthHdr(agent()->GetDhcpProto()->ip_fabric_interface_mac(),
           MacAddress(in_pkt_info.eth->ether_dhost), ETHERTYPE_IP);
    MacAddress tmp(in_pkt_info.eth->ether_dhost);
    pkt_info_->len += sizeof(ether_header);

    return true;
}

bool DhcpHandler::CreateRelayResponsePacket() {
    PktInfo in_pkt_info = *pkt_info_.get();
    pkt_info_->pkt = new uint8_t[DHCP_PKT_SIZE];
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = vm_itf_->vrf()->vrf_id();
    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt +
        sizeof(struct ether_header) + sizeof(agent_hdr));
    pkt_info_->ip = (struct ip *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - DHCP_FIXED_LEN - 4;
    uint16_t opt_len = 4;
    DhcpOptions *read_opt = (DhcpOptions *)(dhcp_->options + 4);
    DhcpOptions *write_opt = (DhcpOptions *)(dhcp->options + 4);
    while ((opt_rem_len > 0) && (read_opt->code != DHCP_OPTION_END)) {
        switch (read_opt->code) {
            case DHCP_OPTION_PAD:
                write_opt->WriteByte(DHCP_OPTION_PAD, &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                opt_rem_len -= 1;
                read_opt = read_opt->GetNextOptionPtr();
                continue;

            case DHCP_OPTION_82:
                break;

            case DHCP_OPTION_MSG_TYPE:
                msg_type_ = *(uint8_t *)read_opt->data;
                write_opt->WriteData(read_opt->code, read_opt->len, &msg_type_, &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

            default:
                write_opt->WriteData(read_opt->code, read_opt->len, &read_opt->data, &opt_len);
                write_opt = write_opt->GetNextOptionPtr();
                break;

        }
        opt_rem_len -= (2 + read_opt->len);
        read_opt = read_opt->GetNextOptionPtr();
    }
    dhcp_ = dhcp;
    dhcp->giaddr = 0;
    config_.client_name_ = vm_itf_->vm_name();
    write_opt->WriteData(DHCP_OPTION_HOST_NAME, config_.client_name_.size(),
                         config_.client_name_.c_str(), &opt_len);
    write_opt = write_opt->GetNextOptionPtr();
    pkt_info_->sport = DHCP_SERVER_PORT;
    pkt_info_->dport = DHCP_CLIENT_PORT;
    write_opt->WriteByte(DHCP_OPTION_END, &opt_len);
    pkt_info_->len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(pkt_info_->len, agent()->router_id().to_ulong(), pkt_info_->sport,
           0xFFFFFFFF, pkt_info_->dport);
    pkt_info_->len += sizeof(ip);
    IpHdr(pkt_info_->len, htonl(agent()->router_id().to_ulong()),
          0xFFFFFFFF, IPPROTO_UDP);
    EthHdr(agent()->pkt()->pkt_handler()->mac_address(),
    MacAddress(dhcp->chaddr), 0x800);
    pkt_info_->len += sizeof(ether_header);
    return true;
}

void DhcpHandler::RelayRequestToFabric() {
    CreateRelayPacket();
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Send(pkt_info_->len, dhcp_proto->ip_fabric_interface_index(),
         pkt_info_->vrf, AgentHdr::TX_SWITCH, PktHandler::DHCP);
    dhcp_proto->IncrStatsRelayReqs();
}

void DhcpHandler::RelayResponseFromFabric() {
    if (!CreateRelayResponsePacket()) {
        DHCP_TRACE(Trace, "Ignoring received DHCP packet from fabric interface");
        return;
    }

    if (msg_type_ == DHCP_ACK) {
        // Populate the DHCP Snoop table
        agent()->interface_table()->AddDhcpSnoopEntry
            (vm_itf_->name(), Ip4Address(ntohl(dhcp_->yiaddr)));
        // Enqueue RESYNC to update the IP address
        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vm_itf_->GetUuid(),
                                         vm_itf_->name()));
        req.data.reset(new VmInterfaceIpAddressData());
        agent()->interface_table()->Enqueue(&req);
    }

    Send(pkt_info_->len, vm_itf_index_,
         pkt_info_->vrf, AgentHdr::TX_SWITCH, PktHandler::DHCP);
    agent()->GetDhcpProto()->IncrStatsRelayResps();
}

// Add option taking no data (length = 0)
uint16_t DhcpHandler::AddNoDataOption(uint32_t option, uint16_t opt_len) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    return opt_len;
}

// Add option taking a byte from input
uint16_t DhcpHandler::AddByteOption(uint32_t option, uint16_t opt_len,
                                    const std::string &input) {
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail() || data > 0xFF) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << " data : " <<
                   input << "; invalid data");
    } else {
        uint8_t byte = (uint8_t)data;
        DhcpOptions *opt = GetNextOptionPtr(opt_len);
        opt->WriteData(option, 1, &byte, &opt_len);
    }
    return opt_len;
}

// Add option taking array of bytes from input
uint16_t DhcpHandler::AddByteArrayOption(uint32_t option, uint16_t opt_len,
                                         const std::string &input) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    uint8_t byte = 0;
    value >> byte;
    while (!value.bad() && !value.fail()) {
        opt->AppendData(1, &byte, &opt_len);
        if (value.eof()) break;
        value >> byte;
    }

    // if atleast one byte is not added, ignore this option
    if (!opt->len || !value.eof()) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << "; data missing");
        return opt_len - opt->len - 2;
    }

    return opt_len;
}

// Add option taking a byte followed by a string, from input
uint16_t DhcpHandler::AddByteStringOption(uint32_t option, uint16_t opt_len,
                                          const std::string &input) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.fail() || value.bad() || data > 0xFF) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << " data : " <<
                   input << "; wrong data");
        return opt_len;
    }

    uint8_t byte = (uint8_t)data;
    std::string str;
    value >> str;
    opt->WriteData(option, 1, &byte, &opt_len);
    opt->AppendData(str.length(), str.c_str(), &opt_len);

    return opt_len;
}

// Add option taking a byte followed by one or more IPs, from input
uint16_t DhcpHandler::AddByteIPOption(uint32_t option, uint16_t opt_len,
                                      const std::string &input) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    uint8_t byte = (uint8_t)data;

    if (value.fail() || value.bad() || data > 0xFF) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << " data : " <<
                   input << "; wrong data");
        return opt_len;
    }

    opt->WriteData(option, 1, &byte, &opt_len);
    while (value.good()) {
        std::string ipstr;
        value >> ipstr;
        opt_len = AddIP(opt, opt_len, ipstr);
    }

    // if atleast one IP is not added, ignore this option
    if (opt->len == 1) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << "; IP missing");
        return opt_len - opt->len - 2;
    }

    return opt_len;
}

// Add option taking string from input
uint16_t DhcpHandler::AddStringOption(uint32_t option, uint16_t opt_len,
                                      const std::string &input) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, input.length(), input.c_str(), &opt_len);
    return opt_len;
}

// Add option taking integer from input
uint16_t DhcpHandler::AddIntegerOption(uint32_t option, uint16_t opt_len,
                                       const std::string &input) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail()) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << " data : " <<
                   input << "; invalid data");
    } else {
        data = htonl(data);
        opt->WriteData(option, 4, &data, &opt_len);
    }
    return opt_len;
}

// Add option taking array of short from input
uint16_t DhcpHandler::AddShortArrayOption(uint32_t option, uint16_t opt_len,
                                          const std::string &input,
                                          bool array) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    uint16_t data = 0;
    value >> data;
    while (!value.bad() && !value.fail()) {
        data = htons(data);
        opt->AppendData(2, &data, &opt_len);
        if (!array || value.eof()) break;
        value >> data;
    }

    // if atleast one short is not added, ignore this option
    if (!opt->len || !value.eof()) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << " data : " <<
                   input << "; invalid data");
        return opt_len - opt->len - 2;
    }

    return opt_len;
}

uint16_t DhcpHandler::AddIpOption(uint32_t option, uint16_t opt_len,
                                  const std::string &input,
                                  uint8_t min_count, uint8_t max_count,
                                  uint8_t multiples) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string ipstr;
        value >> ipstr;
        opt_len = AddIP(opt, opt_len, ipstr);
    }

    if (option == DHCP_OPTION_DNS) {
        // Add DNS servers from the IPAM dns method and server config also
        opt_len = AddDnsServers(opt, opt_len);
    } else if (option == DHCP_OPTION_ROUTER) {
        // Add our gw as well
        if (config_.gw_addr) {
            uint32_t gw = htonl(config_.gw_addr);
            opt->AppendData(4, &gw, &opt_len);
        }
    }

    // check that atleast min_count IP addresses are added
    if (min_count && opt->len < min_count * 4) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << "; data missing");
        return opt_len - opt->len - 2;
    }

    // if specified, check that we dont add more than max_count IP addresses
    if (max_count && opt->len > max_count * 4) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << "; invalid data");
        return opt_len - opt->len - 2;
    }

    // if specified, check that we add in multiples of IP addresses
    if (multiples && opt->len % (multiples * 4)) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << "; invalid data");
        return opt_len - opt->len - 2;
    }

    return opt_len;
}

// Add an IP address to the option
uint16_t DhcpHandler::AddIP(DhcpOptions *opt, uint16_t opt_len,
                            const std::string &input) {
    boost::system::error_code ec;
    uint32_t ip = Ip4Address::from_string(input, ec).to_ulong();
    if (!ec.value() && ip) {
        ip = htonl(ip);
        opt->AppendData(4, &ip, &opt_len);
    } else {
        DHCP_TRACE(Error, "Invalid DHCP option " << opt->code << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() <<
                   "; has to be IP address");
    }
    return opt_len;
}

uint16_t DhcpHandler::AddCompressedName(uint32_t option, uint16_t opt_len,
                                        const std::string &input) {
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string str;
        value >> str;
        if (str.size()) {
            uint8_t name[str.size() * 2 + 2];
            uint16_t len = 0;
            BindUtil::AddName(name, str, 0, 0, len);
            opt->AppendData(len, name, &opt_len);
        }
    }

    if (!opt->len) {
        DHCP_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                   Ip4Address(config_.ip_addr).to_string() << "; invalid data");
        return opt_len - opt->len - 2;
    }

    return opt_len;
}

// Add an DNS server addresses from IPAM to the option
uint16_t DhcpHandler::AddDnsServers(DhcpOptions *opt, uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "default-dns-server" ||
        ipam_type_.ipam_dns_method == "virtual-dns-server" ||
        ipam_type_.ipam_dns_method == "") {
        if (config_.dns_addr) {
            uint32_t dns_addr = htonl(config_.dns_addr);
            opt->AppendData(4, &dns_addr, &opt_len);
        }
    } else if (ipam_type_.ipam_dns_method == "tenant-dns-server") {
        for (unsigned int i = 0; i < ipam_type_.ipam_dns_server.
             tenant_dns_server_address.ip_address.size(); ++i) {
            opt_len = AddIP(opt, opt_len,
                            ipam_type_.ipam_dns_server.
                            tenant_dns_server_address.ip_address[i]);
        }
    }
    return opt_len;
}

// Add domain name from IPAM to the option
uint16_t DhcpHandler::AddDomainNameOption(DhcpOptions *opt, uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        if (config_.domain_name_.size()) {
            opt->WriteData(DHCP_OPTION_DOMAIN_NAME, 0, NULL, &opt_len);
            opt->AppendData(config_.domain_name_.size(),
                            config_.domain_name_.c_str(), &opt_len);
        }
    }
    return opt_len;
}

// Read the list of <subnet/plen, gw> from input and store them
// for later processing in AddClasslessRouteOption
void DhcpHandler::ReadClasslessRoute(uint32_t option, uint16_t opt_len,
                                     const std::string &input) {
    std::stringstream value(input);
    while (value.good()) {
        std::string snetstr;
        value >> snetstr;
        OperDhcpOptions::Subnet subnet;
        boost::system::error_code ec = Ip4PrefixParse(snetstr, &subnet.prefix_,
                                                      (int *)&subnet.plen_);
        if (ec || subnet.plen_ > 32 || !value.good()) {
            DHCP_TRACE(Error, "Invalid Classless route DHCP option for VM " <<
                       Ip4Address(config_.ip_addr).to_string() <<
                       "; has to be list of <subnet/plen gw>");
            break;
        }
        host_routes_.push_back(subnet);

        // ignore the gw, as we use always use subnet's gw
        value >> snetstr;
    }
}

// Add the option defined at the highest level in priority
// Take all options defined at VM interface level, take those from subnet which
// are not configured at interface, then take those from ipam which are not
// configured at interface or subnet.
uint16_t DhcpHandler::AddConfigDhcpOptions(uint16_t opt_len) {
    std::vector<autogen::DhcpOptionType> options;
    if (vm_itf_->GetInterfaceDhcpOptions(&options))
        opt_len = AddDhcpOptions(opt_len, options, InterfaceLevel);

    if (vm_itf_->GetSubnetDhcpOptions(&options))
        opt_len = AddDhcpOptions(opt_len, options, SubnetLevel);

    if (vm_itf_->GetIpamDhcpOptions(&options))
        opt_len = AddDhcpOptions(opt_len, options, IpamLevel);

    return opt_len;
}

uint16_t DhcpHandler::AddDhcpOptions(
                        uint16_t opt_len,
                        std::vector<autogen::DhcpOptionType> &options,
                        DhcpOptionLevel level) {
    for (unsigned int i = 0; i < options.size(); ++i) {
        // if the option name is a number, use it as DHCP code
        // otherwise, use it as option name
        std::stringstream str(options[i].dhcp_option_name);
        uint32_t option = 0;
        str >> option;
        if (!option) {
            option =
                OptionCode(boost::to_lower_copy(options[i].dhcp_option_name));
            if (!option) {
                DHCP_TRACE(Trace, "Invalid DHCP option : " <<
                           options[i].dhcp_option_name << " for VM " <<
                           Ip4Address(config_.ip_addr).to_string());
                continue;
            }
        }

        // if option is already set in the response (from higher level), ignore
        if (is_flag_set(option))
            continue;

        uint16_t old_opt_len = opt_len;
        DhcpOptionCategory category = OptionCategory(option);

        switch(category) {
            case None:
                break;

            case NoData:
                opt_len = AddNoDataOption(option, opt_len);
                break;

            case Bool:
            case Byte:
                opt_len = AddByteOption(option, opt_len,
                                        options[i].dhcp_option_value);
                break;

            case ByteArray:
                opt_len = AddByteArrayOption(option, opt_len,
                                             options[i].dhcp_option_value);
                break;

            case ByteString:
                opt_len = AddByteStringOption(option, opt_len,
                                              options[i].dhcp_option_value);
                break;

            case ByteOneIPPlus:
                opt_len = AddByteIPOption(option, opt_len,
                                          options[i].dhcp_option_value);
                break;

            case String:
                opt_len = AddStringOption(option, opt_len,
                                          options[i].dhcp_option_value);
                break;

            case Int32bit:
            case Uint32bit:
                opt_len = AddIntegerOption(option, opt_len,
                                           options[i].dhcp_option_value);
                break;

            case Uint16bit:
                opt_len = AddShortArrayOption(option, opt_len,
                                              options[i].dhcp_option_value,
                                              false);
                break;

            case Uint16bitArray:
                opt_len = AddShortArrayOption(option, opt_len,
                                              options[i].dhcp_option_value,
                                              true);
                break;

            case OneIP:
                opt_len = AddIpOption(option, opt_len,
                                      options[i].dhcp_option_value, 1, 1, 0);
                break;

            case ZeroIPPlus:
                opt_len = AddIpOption(option, opt_len,
                                      options[i].dhcp_option_value, 0, 0, 0);
                break;

            case OneIPPlus:
                opt_len = AddIpOption(option, opt_len,
                                      options[i].dhcp_option_value, 1, 0, 0);
                break;

            case TwoIPPlus:
                opt_len = AddIpOption(option, opt_len,
                                      options[i].dhcp_option_value, 2, 0, 2);
                break;

            case ClasslessRoute:
                ReadClasslessRoute(option, opt_len,
                                   options[i].dhcp_option_value);
                if (host_routes_.size()) {
                    // set flag & level for classless route so that we dont
                    // update these at other level (subnet or ipam)
                    set_flag(DHCP_OPTION_CLASSLESS_ROUTE);
                    host_routes_level_ = level;
                }
                break;

            case NameCompression:
                opt_len = AddCompressedName(option, opt_len,
                                            options[i].dhcp_option_value);
                break;

            default:
                DHCP_TRACE(Error, "Unsupported DHCP option : " +
                           options[i].dhcp_option_name);
                break;
        }

        if (opt_len != old_opt_len)
            set_flag(option);
    }

    return opt_len;
}

// Add host route options coming via config. Config priority is
// 1) options at VM interface level (host routes specifically set)
// 2) options at VM interface level (classless routes from --extra-dhcp-opts)
// 3) options at subnet level (host routes at subnet level - neutron config)
// 4) options at subnet level (classless routes from dhcp options)
// 5) options at IPAM level (host routes from IPAM)
// 6) options at IPAM level (classless routes from IPAM dhcp options)
// Add the options defined at the highest level in priority
uint16_t DhcpHandler::AddClasslessRouteOption(uint16_t opt_len) {
    std::vector<OperDhcpOptions::Subnet> host_routes;
    do {
        // Port specific host route options
        // TODO: should we remove host routes at port level from schema ?
        if (vm_itf_->oper_dhcp_options().are_host_routes_set()) {
            host_routes = vm_itf_->oper_dhcp_options().host_routes();
            break;
        }
        // Host routes from port specific DHCP options (neutron configuration)
        if (host_routes_level_ == InterfaceLevel && host_routes_.size()) {
            host_routes.swap(host_routes_);
            break;
        }

        if (vm_itf_->vn()) {
            Ip4Address ip(config_.ip_addr);
            const std::vector<VnIpam> &vn_ipam = vm_itf_->vn()->GetVnIpam();
            uint32_t index;
            for (index = 0; index < vn_ipam.size(); ++index) {
                if (vn_ipam[index].IsSubnetMember(ip)) {
                    break;
                }
            }
            if (index < vn_ipam.size()) {
                // Subnet level host route options
                // Preference to host routes at subnet (neutron configuration)
                if (vn_ipam[index].oper_dhcp_options.are_host_routes_set()) {
                    host_routes = vn_ipam[index].oper_dhcp_options.host_routes();
                    break;
                }
                // Host route options from subnet level DHCP options
                if (host_routes_level_ == SubnetLevel && host_routes_.size()) {
                    host_routes.swap(host_routes_);
                    break;
                }
            }

            // TODO: should remove host routes at VN level from schema ?
            vm_itf_->vn()->GetVnHostRoutes(ipam_name_, &host_routes);
            if (host_routes.size() > 0)
                break;
        }

        // IPAM level host route options
        const std::vector<autogen::RouteType> &routes =
            ipam_type_.host_routes.route;
        for (unsigned int i = 0; i < routes.size(); ++i) {
            OperDhcpOptions::Subnet subnet;
            boost::system::error_code ec = Ip4PrefixParse(routes[i].prefix,
                                                          &subnet.prefix_,
                                                          (int *)&subnet.plen_);
            if (ec || subnet.plen_ > 32) {
                continue;
            }
            host_routes.push_back(subnet);
        }
        // Host route options from IPAM level DHCP options
        if (!host_routes.size() && host_routes_.size()) {
            host_routes.swap(host_routes_);
            break;
        }
    } while (false);

    if (host_routes.size()) {
        DhcpOptions *opt = GetNextOptionPtr(opt_len);
        opt->code = DHCP_OPTION_CLASSLESS_ROUTE;
        uint8_t *ptr = opt->data;
        uint8_t len = 0;
        for (uint32_t i = 0; i < host_routes.size(); ++i) {
            uint32_t prefix = host_routes[i].prefix_.to_ulong();
            uint32_t plen = host_routes[i].plen_;
            *ptr++ = plen;
            len++;
            for (unsigned int i = 0; plen && i <= (plen - 1) / 8; ++i) {
                *ptr++ = (prefix >> 8 * (3 - i)) & 0xFF;
                len++;
            }
            *(uint32_t *)ptr = htonl(config_.gw_addr);
            ptr += sizeof(uint32_t);
            len += sizeof(uint32_t);
        }
        opt->len = len;
        opt_len += 2 + len;
        set_flag(DHCP_OPTION_CLASSLESS_ROUTE);
    }
    return opt_len;
}

uint16_t DhcpHandler::DhcpHdr(in_addr_t yiaddr, in_addr_t siaddr) {
    dhcp_->op = BOOT_REPLY;
    dhcp_->htype = HW_TYPE_ETHERNET;
    dhcp_->hlen = ETHER_ADDR_LEN;
    dhcp_->hops = 0;
    dhcp_->xid = request_.xid;
    dhcp_->secs = 0;
    dhcp_->flags = htons(request_.flags);
    dhcp_->ciaddr = 0;
    dhcp_->yiaddr = yiaddr;
    dhcp_->siaddr = siaddr;
    dhcp_->giaddr = 0;
    memset (dhcp_->chaddr, 0, DHCP_CHADDR_LEN);
    MacAddress(request_.mac_addr).ToArray(dhcp_->chaddr,
               sizeof(dhcp_->chaddr));
    // not supporting dhcp_->sname, dhcp_->file for now
    memset(dhcp_->sname, '\0', DHCP_NAME_LEN);
    memset(dhcp_->file, '\0', DHCP_FILE_LEN);

    memcpy(dhcp_->options, DHCP_OPTIONS_COOKIE, 4);

    uint16_t opt_len = 4;
    DhcpOptions *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(DHCP_OPTION_MSG_TYPE, 1, &out_msg_type_, &opt_len);

    opt = GetNextOptionPtr(opt_len);
    opt->WriteData(DHCP_OPTION_SERVER_IDENTIFIER, 4, &siaddr, &opt_len);

    if (out_msg_type_ == DHCP_NAK) {
        opt = GetNextOptionPtr(opt_len);
        opt->WriteData(DHCP_OPTION_MESSAGE, nak_msg_.size(),
                       nak_msg_.data(), &opt_len);
    }
    else {
        if (msg_type_ != DHCP_INFORM) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_IP_LEASE_TIME,
                           config_.lease_time, &opt_len);
        }

        if (config_.subnet_mask) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_SUBNET_MASK, config_.subnet_mask, &opt_len);
        }

        if (config_.bcast_addr) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_BCAST_ADDRESS, config_.bcast_addr, &opt_len);
        }

        // Add dhcp options coming from Config
        opt_len = AddConfigDhcpOptions(opt_len);

        // Add classless route option
        opt_len = AddClasslessRouteOption(opt_len);

        if (!is_flag_set(DHCP_OPTION_CLASSLESS_ROUTE) &&
            !is_flag_set(DHCP_OPTION_ROUTER) && config_.gw_addr) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteWord(DHCP_OPTION_ROUTER, config_.gw_addr, &opt_len);
        }

        if (!is_flag_set(DHCP_OPTION_HOST_NAME) &&
            config_.client_name_.size()) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteData(DHCP_OPTION_HOST_NAME, config_.client_name_.size(),
                           config_.client_name_.c_str(), &opt_len);
        }

        if (!is_flag_set(DHCP_OPTION_DNS)) {
            opt = GetNextOptionPtr(opt_len);
            opt->WriteData(DHCP_OPTION_DNS, 0, NULL, &opt_len);
            opt_len = AddDnsServers(opt, opt_len);
        }

        if (!is_flag_set(DHCP_OPTION_DOMAIN_NAME)) {
            opt = GetNextOptionPtr(opt_len);
            opt_len = AddDomainNameOption(opt, opt_len);
        }
    }

    opt = GetNextOptionPtr(opt_len);
    opt->code = DHCP_OPTION_END;
    opt_len += 1;

    return (DHCP_FIXED_LEN + opt_len);
}

uint16_t DhcpHandler::FillDhcpResponse(MacAddress &dest_mac,
                                       in_addr_t src_ip, in_addr_t dest_ip,
                                       in_addr_t siaddr, in_addr_t yiaddr) {

    pkt_info_->eth = (ether_header *)(pkt_info_->pkt + EncapHeaderLen());
    EthHdr(agent()->pkt()->pkt_handler()->mac_address(), dest_mac,
           ETHERTYPE_IP);

    uint16_t header_len = sizeof(ether_header);

    if (vm_itf_->vlan_id() != VmInterface::kInvalidVlanId) {
        // cfi and priority are zero
        VlanHdr(pkt_info_->pkt + EncapHeaderLen() + 12, vm_itf_->vlan_id());
        header_len += sizeof(vlanhdr);
    }
    pkt_info_->ip = (ip *)(pkt_info_->pkt + EncapHeaderLen() + header_len);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);
    dhcp_ = dhcp;

    uint16_t len = DhcpHdr(yiaddr, siaddr);
    len += sizeof(udphdr);
    UdpHdr(len, src_ip, DHCP_SERVER_PORT, dest_ip, DHCP_CLIENT_PORT);

    len += sizeof(ip);

    IpHdr(len, src_ip, dest_ip, IPPROTO_UDP);

    return len + header_len;
}

void DhcpHandler::SendDhcpResponse() {
    // TODO: If giaddr is set, what to do ?

    in_addr_t src_ip = htonl(config_.gw_addr);
    in_addr_t dest_ip = 0xFFFFFFFF;
    in_addr_t yiaddr = htonl(config_.ip_addr);
    in_addr_t siaddr = src_ip;
    MacAddress dest_mac;
    dest_mac.Broadcast();

    // If requested IP address is not available, send NAK
    if ((msg_type_ == DHCP_REQUEST) && (request_.ip_addr) &&
        (config_.ip_addr != request_.ip_addr)) {
        out_msg_type_ = DHCP_NAK;
        yiaddr = 0;
        siaddr = 0;
    }

    // send a unicast response when responding to INFORM
    // or when incoming giaddr is zero and ciaddr is set
    // or when incoming bcast flag is not set (with giaddr & ciaddr being zero)
    if ((msg_type_ == DHCP_INFORM) ||
        (!dhcp_->giaddr && (dhcp_->ciaddr ||
                            !(request_.flags & DHCP_BCAST_FLAG)))) {
        dest_ip = yiaddr;
        dest_mac = dhcp_->chaddr;
        if (msg_type_ == DHCP_INFORM)
            yiaddr = 0;
    }

    UpdateStats();

    uint16_t len = FillDhcpResponse(dest_mac, src_ip, dest_ip, siaddr, yiaddr);
    Send(len, GetInterfaceIndex(), pkt_info_->vrf,
         AgentHdr::TX_SWITCH, PktHandler::DHCP);
}

void DhcpHandler::UpdateStats() {
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    (out_msg_type_ == DHCP_OFFER) ? dhcp_proto->IncrStatsOffers() :
        ((out_msg_type_ == DHCP_ACK) ? dhcp_proto->IncrStatsAcks() :
                                       dhcp_proto->IncrStatsNacks());
}

DhcpHandler::DhcpOptionCategory
DhcpHandler::OptionCategory(uint32_t option) const {
    Dhcpv4CategoryIter iter = g_dhcpv4_category_map.find(option);
    if (iter == g_dhcpv4_category_map.end())
        return None;
    return iter->second;
}

uint32_t DhcpHandler::OptionCode(const std::string &option) const {
    Dhcpv4NameCodeIter iter = g_dhcpv4_namecode_map.find(option);
    if (iter == g_dhcpv4_namecode_map.end())
        return 0;
    return iter->second;
}
