/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
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
        // (DHCP_OPTION_SUBNET_MASK, DhcpHandler::OneIPv4)       // agent adds this option
        (DHCP_OPTION_TIME_OFFSET, DhcpHandler::Int32bit)
        (DHCP_OPTION_ROUTER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_TIME_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_NAME_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_DNS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_LOG_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_QUOTE_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_LPR_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_IMPRESS_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_RESOURCE_LOCATION_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_HOST_NAME, DhcpHandler::String)
        (DHCP_OPTION_BOOT_FILE_SIZE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_MERIT_DUMP_FILE, DhcpHandler::String)
        (DHCP_OPTION_DOMAIN_NAME, DhcpHandler::String)
        (DHCP_OPTION_SWAP_SERVER, DhcpHandler::OneIPv4)
        (DHCP_OPTION_ROOT_PATH, DhcpHandler::String)
        (DHCP_OPTION_EXTENSION_PATH, DhcpHandler::String)
        (DHCP_OPTION_IP_FWD_CONTROL, DhcpHandler::Bool)
        (DHCP_OPTION_NL_SRC_ROUTING, DhcpHandler::Bool)
        (DHCP_OPTION_POLICY_FILTER, DhcpHandler::TwoIPv4Plus)
        (DHCP_OPTION_MAX_DG_REASSEMBLY_SIZE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_DEFAULT_IP_TTL, DhcpHandler::Byte)
        (DHCP_OPTION_PATH_MTU_AGING_TIMEOUT, DhcpHandler::Uint32bit)
        (DHCP_OPTION_PATH_MTU_PLATEAU_TABLE, DhcpHandler::Uint16bitArray)
        (DHCP_OPTION_INTERFACE_MTU, DhcpHandler::Uint16bit)
        (DHCP_OPTION_ALL_SUBNETS_LOCAL, DhcpHandler::Bool)
        // (DHCP_OPTION_BCAST_ADDRESS, DhcpHandler::OneIPv4)     // agent adds this option
        (DHCP_OPTION_PERFORM_MASK_DISCOVERY, DhcpHandler::Bool)
        (DHCP_OPTION_MASK_SUPPLIER, DhcpHandler::Bool)
        (DHCP_OPTION_PERFORM_ROUTER_DISCOVERY, DhcpHandler::Bool)
        (DHCP_OPTION_ROUTER_SOLICIT_ADDRESS, DhcpHandler::OneIPv4)
        (DHCP_OPTION_STATIC_ROUTING_TABLE, DhcpHandler::TwoIPv4Plus)
        (DHCP_OPTION_TRAILER_ENCAP, DhcpHandler::Bool)
        (DHCP_OPTION_ARP_CACHE_TIMEOUT, DhcpHandler::Uint32bit)
        (DHCP_OPTION_ETHERNET_ENCAP, DhcpHandler::Bool)
        (DHCP_OPTION_DEFAULT_TCP_TTL, DhcpHandler::Byte)
        (DHCP_OPTION_TCP_KEEPALIVE_INTERVAL, DhcpHandler::Uint32bit)
        (DHCP_OPTION_TCP_KEEPALIVE_GARBAGE, DhcpHandler::Bool)
        (DHCP_OPTION_NIS_DOMAIN, DhcpHandler::String)
        (DHCP_OPTION_NIS_SERVERS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_NTP_SERVERS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_VENDOR_SPECIFIC_INFO, DhcpHandler::String)
        (DHCP_OPTION_NETBIOS_OVER_TCP_NS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_NETBIOS_OVER_TCP_DG_DS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_NETBIOS_OVER_TCP_NODE_TYPE, DhcpHandler::Byte)
        (DHCP_OPTION_NETBIOS_OVER_TCP_SCOPE, DhcpHandler::String)
        (DHCP_OPTION_XWINDOW_FONT_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_XWINDOW_SYSTEM_DISP_MGR, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_REQ_IP_ADDRESS, DhcpHandler::OneIPv4)
        // (DHCP_OPTION_IP_LEASE_TIME, DhcpHandler::Uint32bit) // agent adds this option
        (DHCP_OPTION_OVERLOAD, DhcpHandler::Byte)
        // (DHCP_OPTION_MSG_TYPE, DhcpHandler::Byte)           // agent adds this option
        // (DHCP_OPTION_SERVER_IDENTIFIER, DhcpHandler::OneIPv4) // agent adds this option
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
        (DHCP_OPTION_NIS_PLUS_SERVERS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_TFTP_SERVER_NAME, DhcpHandler::String)
        (DHCP_OPTION_BOOTFILE_NAME, DhcpHandler::String)
        (DHCP_OPTION_MOBILE_IP_HA, DhcpHandler::ZeroIPv4Plus)
        (DHCP_OPTION_SMTP_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_POP_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_NNTP_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_DEFAULT_WWW_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_DEFAULT_FINGER_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_DEFAULT_IRC_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_STREETTALK_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_STREETTALK_DA_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_USER_CLASS_INFO, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_SLP_DIRECTORY_AGENT, DhcpHandler::ByteOneIPPlus)
        (DHCP_OPTION_SLP_SERVICE_SCOPE, DhcpHandler::ByteString)
        (DHCP_OPTION_RAPID_COMMIT, DhcpHandler::NoData)
        // (DHCP_OPTION_CLIENT_FQDN, DhcpHandler::ByteArray)   // sent by clients
        (DHCP_OPTION_STORAGE_NS, DhcpHandler::ByteArray)       // send encoded data
        (DHCP_OPTION_NDS_SERVERS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_NDS_TREE_NAME, DhcpHandler::ByteArray)    // send encoded data
        (DHCP_OPTION_NDS_CONTEXT, DhcpHandler::ByteArray)      // send encoded data
        (DHCP_OPTION_BCMCS_DN_LIST, DhcpHandler::ByteArray)    // send encoded data
        (DHCP_OPTION_BCMCS_ADDR_LIST, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_AUTH, DhcpHandler::ByteArray)             // send encoded data
        (DHCP_OPTION_CLIENT_LAST_XTIME, DhcpHandler::Uint32bit)
        (DHCP_OPTION_ASSOCIATE_IP, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_CLIENT_SYSARCH_TYPE, DhcpHandler::Uint16bit)
        (DHCP_OPTION_CLIENT_NW_INTERFACE_ID, DhcpHandler::ByteArray)
        (DHCP_OPTION_LDAP, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_CLIENT_MACHINE_ID, DhcpHandler::String)
        (DHCP_OPTION_OPENGROUP_USER_AUTH, DhcpHandler::String)
        (DHCP_OPTION_GEOCONF_CIVIC, DhcpHandler::ByteArray)    // send encoded data
        (DHCP_OPTION_IEEE_1003_1_TZ, DhcpHandler::String)
        (DHCP_OPTION_REF_TZ_DB, DhcpHandler::String)
        (DHCP_OPTION_NETINFO_PARENT_SERVER_TAG, DhcpHandler::String)
        (DHCP_OPTION_NETINFO_PARENT_SERVER_ADDR, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_URL, DhcpHandler::String)
        (DHCP_OPTION_AUTO_CONFIGURE, DhcpHandler::Bool)
        (DHCP_OPTION_NAME_SERVICE_SEARCH, DhcpHandler::Uint16bitArray)
        (DHCP_OPTION_SUBNET_SELECTION, DhcpHandler::OneIPv4)
        (DHCP_OPTION_DNS_DOMAIN_SEARCH_LIST, DhcpHandler::NameCompression)
        (DHCP_OPTION_SIP_SERVERS, DhcpHandler::ByteArray)                // send encoded data
        (DHCP_OPTION_CLASSLESS_ROUTE, DhcpHandler::ClasslessRoute)
        (DHCP_OPTION_CCC, DhcpHandler::ByteArray)                        // send encoded data
        (DHCP_OPTION_GEOCONF, DhcpHandler::ByteArray)                    // send encoded data
        (DHCP_OPTION_VENDOR_ID_VENDOR_CLASS, DhcpHandler::ByteArray)     // send encoded data
        (DHCP_OPTION_VENDOR_ID_VENDOR_SPECIFIC, DhcpHandler::ByteArray)  // send encoded data
        (DHCP_OPTION_TFTP_SERVER, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_129, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_130, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_131, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_132, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_133, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_134, DhcpHandler::String)
        (DHCP_OPTION_PXE_VENDOR_SPECIFIC_135, DhcpHandler::String)
        (DHCP_OPTION_PANA_AUTH_AGENT, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_LOST_SERVER, DhcpHandler::ByteArray)         // send encoded data
        (DHCP_OPTION_CAPWAP_AC_ADDRESS, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_IPV4_ADDRESS_MOS, DhcpHandler::ByteArray)     // send encoded data
        (DHCP_OPTION_IPV4_FQDN_MOS, DhcpHandler::ByteArray)        // send encoded data
        (DHCP_OPTION_SIP_UA_CONFIG_DOMAIN, DhcpHandler::ByteArray) // send encoded data
        (DHCP_OPTION_IPV4_ADDRESS_ANDSF, DhcpHandler::OneIPv4Plus)
        (DHCP_OPTION_GEOLOC, DhcpHandler::ByteArray)               // send encoded data
        (DHCP_OPTION_FORCERENEW_NONCE_CAP, DhcpHandler::ByteArray) // send encoded data
        (DHCP_OPTION_RDNSS_SELECTION, DhcpHandler::ByteArray)      // send encoded data
        (DHCP_OPTION_TFTP_SERVER_ADDRESS, DhcpHandler::OneIPv4Plus)
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
        : DhcpHandlerBase(agent, info, io), msg_type_(DHCP_UNKNOWN),
          out_msg_type_(DHCP_UNKNOWN),
          nak_msg_("cannot assign requested address") {
    option_.reset(new Dhcpv4OptionHandler(NULL));
}

DhcpHandler::~DhcpHandler() {
}

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
    option_->SetDhcpOptionPtr(dhcp_->options);
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
    if (!vm_itf_->layer3_forwarding()) {
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
    int16_t options_len = pkt_info_->len - sizeof(struct ether_header) -
        sizeof(struct ip) - sizeof(udphdr) - DHCP_FIXED_LEN;
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
        SendDhcpResponse();
        DHCP_TRACE(Trace, "DHCP response sent; message = " <<
                   ServicesSandesh::DhcpMsgType(out_msg_type_) <<
                   "; ip = " << config_.ip_addr.to_string());
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
    option_->SetDhcpOptionPtr(dhcp_->options);
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
    Dhcpv4Options *opt = (Dhcpv4Options *)(dhcp_->options + 4);
    // parse thru the option fields
    while ((opt_rem_len > 0) && (opt->code != DHCP_OPTION_END)) {
        switch (opt->code) {
            case DHCP_OPTION_PAD:
                opt_rem_len -= 1;
                opt = (Dhcpv4Options *)((uint8_t *)opt + 1);
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

            case DHCP_OPTION_PARAMETER_REQUEST_LIST:
                if (opt_rem_len >= opt->len + 2)
                    parameters_.assign((char *)opt->data, opt->len);
                break;

            case DHCP_OPTION_82:
                ReadOption82(opt);
                break;

            default:
                break;
        }
        opt_rem_len -= (2 + opt->len);
        opt = (Dhcpv4Options *)((uint8_t *)opt + 2 + opt->len);
    }

    return true;
}

void DhcpHandler::FillDhcpInfo(Ip4Address &addr, int plen,
                               Ip4Address &gw, Ip4Address &dns) {
    config_.ip_addr = addr;
    config_.plen = plen;
    uint32_t mask = plen? (0xFFFFFFFF << (32 - plen)) : 0;
    config_.subnet_mask = mask;
    config_.bcast_addr = (addr.to_ulong() & mask) | ~mask;
    config_.gw_addr = gw;
    config_.dns_addr = dns;
}


bool DhcpHandler::FindLeaseData() {
    Ip4Address unspecified;
    Ip4Address ip = vm_itf_->ip_addr();
    // Change client name to VM name; this is the name assigned to the VM
    config_.client_name_ = vm_itf_->vm_name();
    FindDomainName(ip);
    if (vm_itf_->ipv4_active()) {
        if (vm_itf_->fabric_port()) {
            InetUnicastRouteEntry *rt =
                InetUnicastAgentRouteTable::FindResolveRoute(
                             vm_itf_->vrf()->GetName(), ip);
            if (rt) {
                Ip4Address gw = agent()->vhost_default_gateway();
                boost::system::error_code ec;
                if (IsIp4SubnetMember(rt->addr().to_v4(),
                    Ip4Address::from_string("169.254.0.0", ec), rt->plen())) {
                    gw = unspecified;
                }
                FillDhcpInfo(ip, rt->plen(), gw, gw);
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
            if (!ipam[i].IsV4()) {
                continue;
            }
            if (IsIp4SubnetMember(ip, ipam[i].ip_prefix.to_v4(), 
                                  ipam[i].plen)) {
                Ip4Address default_gw = ipam[i].default_gw.to_v4();
                FillDhcpInfo(ip, ipam[i].plen, default_gw, default_gw);
                return true;
            }
        }
    }

    // We dont have the config yet; give a short lease
    config_.lease_time = DHCP_SHORTLEASE_TIME;
    if (ip.to_ulong()) {
        // Give address received from Nova
        FillDhcpInfo(ip, 32, unspecified, unspecified);
    } else {
        // Give a link local address
        boost::system::error_code ec;
        Ip4Address gwip = Ip4Address::from_string(LINK_LOCAL_SUBNET, ec);
        gwip = Ip4Address((gwip.to_ulong() & 0xFFFF0000) | (vm_itf_->id() & 0xFF));
        FillDhcpInfo(gwip, 16, unspecified, unspecified);
    }
    return true;
}

void DhcpHandler::WriteOption82(Dhcpv4Options *opt, uint16_t *optlen) {
    optlen += 2;
    opt->code = DHCP_OPTION_82;
    opt->len = sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2;
    Dhcpv4Options *subopt = reinterpret_cast<Dhcpv4Options *>(opt->data);
    uint32_t value = htonl(vm_itf_->id());
    subopt->WriteData(DHCP_SUBOP_CKTID, 4, &value, optlen);
    subopt = subopt->GetNextOptionPtr();
    subopt->WriteData(DHCP_SUBOP_REMOTEID, sizeof(VmInterface *),
                      &vm_itf_, optlen);
}

bool DhcpHandler::ReadOption82(Dhcpv4Options *opt) {
    if (opt->len != sizeof(uint32_t) + 2 + sizeof(VmInterface *) + 2)
        return false;

    Dhcpv4Options *subopt = reinterpret_cast<Dhcpv4Options *>(opt->data);
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

    pkt_info_->AllocPacketBuffer(agent(), PktHandler::DHCP, DHCP_PKT_SIZE, 0);
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = in_pkt_info.vrf;
    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt);
    pkt_info_->ip = (struct ip *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - sizeof(struct ether_header) - sizeof(struct ip)
        - sizeof(udphdr) - DHCP_FIXED_LEN - 4;
    uint16_t opt_len = 4;
    Dhcpv4Options *read_opt = (Dhcpv4Options *)(dhcp_->options + 4);
    Dhcpv4Options *write_opt = (Dhcpv4Options *)(dhcp->options + 4);
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
    option_->SetDhcpOptionPtr(dhcp_->options);
    dhcp->giaddr = htonl(agent()->router_id().to_ulong());
    WriteOption82(write_opt, &opt_len);
    write_opt = write_opt->GetNextOptionPtr();
    pkt_info_->sport = DHCP_SERVER_PORT;
    pkt_info_->dport = DHCP_SERVER_PORT;
    write_opt->WriteByte(DHCP_OPTION_END, &opt_len);

    uint32_t len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(len, in_pkt_info.ip->ip_src.s_addr, pkt_info_->sport,
           in_pkt_info.ip->ip_dst.s_addr, pkt_info_->dport);
    len += sizeof(struct ip);
    IpHdr(len, htonl(agent()->router_id().to_ulong()), 0xFFFFFFFF, IPPROTO_UDP);
    EthHdr(agent()->GetDhcpProto()->ip_fabric_interface_mac(),
           MacAddress(in_pkt_info.eth->ether_dhost), ETHERTYPE_IP);
    len += sizeof(struct ether_header);

    pkt_info_->set_len(len);
    return true;
}

bool DhcpHandler::CreateRelayResponsePacket() {
    PktInfo in_pkt_info = *pkt_info_.get();
    pkt_info_->AllocPacketBuffer(agent(), PktHandler::DHCP, DHCP_PKT_SIZE, 0);
    memset(pkt_info_->pkt, 0, DHCP_PKT_SIZE);
    pkt_info_->vrf = vm_itf_->vrf()->vrf_id();
    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt);
    pkt_info_->ip = (struct ip *)(pkt_info_->eth + 1);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);

    memcpy((uint8_t *)dhcp, (uint8_t *)dhcp_, DHCP_FIXED_LEN);
    memcpy(dhcp->options, DHCP_OPTIONS_COOKIE, 4);

    int16_t opt_rem_len = in_pkt_info.len - DHCP_FIXED_LEN - 4;
    uint16_t opt_len = 4;
    Dhcpv4Options *read_opt = (Dhcpv4Options *)(dhcp_->options + 4);
    Dhcpv4Options *write_opt = (Dhcpv4Options *)(dhcp->options + 4);
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
    option_->SetDhcpOptionPtr(dhcp_->options);
    dhcp->giaddr = 0;
    config_.client_name_ = vm_itf_->vm_name();
    write_opt->WriteData(DHCP_OPTION_HOST_NAME, config_.client_name_.size(),
                         config_.client_name_.c_str(), &opt_len);
    write_opt = write_opt->GetNextOptionPtr();
    pkt_info_->sport = DHCP_SERVER_PORT;
    pkt_info_->dport = DHCP_CLIENT_PORT;
    write_opt->WriteByte(DHCP_OPTION_END, &opt_len);

    uint32_t len = DHCP_FIXED_LEN + opt_len + sizeof(udphdr);

    UdpHdr(len, agent()->router_id().to_ulong(), pkt_info_->sport,
           0xFFFFFFFF, pkt_info_->dport);
    len += sizeof(struct ip);
    IpHdr(len, htonl(agent()->router_id().to_ulong()),
          0xFFFFFFFF, IPPROTO_UDP);
    EthHdr(agent()->vhost_interface()->mac(), MacAddress(dhcp->chaddr),
           ETHERTYPE_IP);
    len += sizeof(struct ether_header);

    pkt_info_->set_len(len);
    return true;
}

void DhcpHandler::RelayRequestToFabric() {
    CreateRelayPacket();
    DhcpProto *dhcp_proto = agent()->GetDhcpProto();
    Send(dhcp_proto->ip_fabric_interface_index(), pkt_info_->vrf,
         AgentHdr::TX_SWITCH, PktHandler::DHCP);
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

    Send(vm_itf_index_, pkt_info_->vrf, AgentHdr::TX_SWITCH, PktHandler::DHCP);
    agent()->GetDhcpProto()->IncrStatsRelayResps();
}

// Add an IP address to the option
uint16_t DhcpHandler::AddIP(uint16_t opt_len, const std::string &input) {
    boost::system::error_code ec;
    uint32_t ip = Ip4Address::from_string(input, ec).to_ulong();
    if (!ec.value() && ip) {
        ip = htonl(ip);
        option_->AppendData(4, &ip, &opt_len);
    } else {
        DHCP_TRACE(Error, "Invalid DHCP option " << option_->GetCode() <<
                   " data : " << input << " for VM " <<
                   config_.ip_addr.to_string() << "; has to be IP address");
    }
    return opt_len;
}

// Add domain name from IPAM to the option
uint16_t DhcpHandler::AddDomainNameOption(uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        if (config_.domain_name_.size()) {
            option_->WriteData(DHCP_OPTION_DOMAIN_NAME, 0, NULL, &opt_len);
            option_->AppendData(config_.domain_name_.size(),
                                config_.domain_name_.c_str(), &opt_len);
        }
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
    memcpy(dhcp_->chaddr, request_.mac_addr, ETHER_ADDR_LEN);
    // not supporting dhcp_->sname, dhcp_->file for now
    memset(dhcp_->sname, '\0', DHCP_NAME_LEN);
    memset(dhcp_->file, '\0', DHCP_FILE_LEN);

    memcpy(dhcp_->options, DHCP_OPTIONS_COOKIE, 4);

    uint16_t opt_len = 4;
    option_->SetNextOptionPtr(opt_len);
    option_->WriteData(DHCP_OPTION_MSG_TYPE, 1, &out_msg_type_, &opt_len);

    option_->SetNextOptionPtr(opt_len);
    option_->WriteData(DHCP_OPTION_SERVER_IDENTIFIER, 4, &siaddr, &opt_len);

    if (out_msg_type_ == DHCP_NAK) {
        option_->SetNextOptionPtr(opt_len);
        option_->WriteData(DHCP_OPTION_MESSAGE, nak_msg_.size(), 
                            nak_msg_.data(), &opt_len);
    }
    else {
        if (msg_type_ != DHCP_INFORM) {
            option_->SetNextOptionPtr(opt_len);
            uint32_t value = htonl(config_.lease_time);
            option_->WriteData(DHCP_OPTION_IP_LEASE_TIME, 4, &value, &opt_len);
        }

        if (config_.subnet_mask) {
            option_->SetNextOptionPtr(opt_len);
            uint32_t value = htonl(config_.subnet_mask);
            option_->WriteData(DHCP_OPTION_SUBNET_MASK, 4, &value, &opt_len);
        }

        if (config_.bcast_addr) {
            option_->SetNextOptionPtr(opt_len);
            uint32_t value = htonl(config_.bcast_addr);
            option_->WriteData(DHCP_OPTION_BCAST_ADDRESS, 4, &value, &opt_len);
        }

        // Add dhcp options coming from Config
        opt_len = AddConfigDhcpOptions(opt_len, false);

        // Add classless route option
        option_->SetNextOptionPtr(opt_len);
        opt_len = AddClasslessRouteOption(opt_len);

        if (IsRouterOptionNeeded()) {
            option_->SetNextOptionPtr(opt_len);
            opt_len = AddIpv4Option(DHCP_OPTION_ROUTER, opt_len,
                                    routers_, 1, 0, 0);
        }

        if (!is_flag_set(DHCP_OPTION_HOST_NAME) &&
            config_.client_name_.size()) {
            option_->SetNextOptionPtr(opt_len);
            option_->WriteData(DHCP_OPTION_HOST_NAME, config_.client_name_.size(),
                               config_.client_name_.c_str(), &opt_len);
        }

        if (!is_flag_set(DHCP_OPTION_DNS)) {
            uint16_t old_opt_len = opt_len;
            option_->SetNextOptionPtr(opt_len);
            option_->WriteData(DHCP_OPTION_DNS, 0, NULL, &opt_len);
            opt_len = AddDnsServers(opt_len);
            if (opt_len == old_opt_len) opt_len -= 4;
        }

        if (!is_flag_set(DHCP_OPTION_DOMAIN_NAME)) {
            option_->SetNextOptionPtr(opt_len);
            opt_len = AddDomainNameOption(opt_len);
        }
    }

    option_->SetNextOptionPtr(opt_len);
    option_->SetCode(DHCP_OPTION_END);
    opt_len += 1;

    return (DHCP_FIXED_LEN + opt_len);
}

uint16_t DhcpHandler::FillDhcpResponse(const MacAddress &dest_mac,
                                       in_addr_t src_ip, in_addr_t dest_ip,
                                       in_addr_t siaddr, in_addr_t yiaddr) {
    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt);
    EthHdr(agent()->vhost_interface()->mac(), dest_mac, ETHERTYPE_IP);
    uint16_t header_len = sizeof(struct ether_header);
    if (vm_itf_->vlan_id() != VmInterface::kInvalidVlanId) {
        // cfi and priority are zero
        VlanHdr(pkt_info_->pkt + 12, vm_itf_->vlan_id());
        header_len += sizeof(vlanhdr);
    }

    pkt_info_->ip = (struct ip *)(pkt_info_->pkt + header_len);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip + 1);
    dhcphdr *dhcp = (dhcphdr *)(pkt_info_->transp.udp + 1);
    dhcp_ = dhcp;
    option_->SetDhcpOptionPtr(dhcp_->options);

    uint16_t len = DhcpHdr(yiaddr, siaddr);
    len += sizeof(udphdr);
    UdpHdr(len, src_ip, DHCP_SERVER_PORT, dest_ip, DHCP_CLIENT_PORT);
    len += sizeof(struct ip);
    IpHdr(len, src_ip, dest_ip, IPPROTO_UDP);

    pkt_info_->set_len(len + header_len);
    return pkt_info_->packet_buffer()->data_len();
}

void DhcpHandler::SendDhcpResponse() {
    // TODO: If giaddr is set, what to do ?

    in_addr_t src_ip = htonl(config_.gw_addr.to_v4().to_ulong());
    in_addr_t dest_ip = 0xFFFFFFFF;
    in_addr_t yiaddr = htonl(config_.ip_addr.to_v4().to_ulong());
    in_addr_t siaddr = src_ip;
    MacAddress dest_mac = MacAddress::BroadcastMac();

    // If requested IP address is not available, send NAK
    if ((msg_type_ == DHCP_REQUEST) && (request_.ip_addr) &&
        (config_.ip_addr.to_v4().to_ulong() != request_.ip_addr)) {
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

    FillDhcpResponse(dest_mac, src_ip, dest_ip, siaddr, yiaddr);
    Send(GetInterfaceIndex(), pkt_info_->vrf, AgentHdr::TX_SWITCH,
         PktHandler::DHCP);
}

// Check if the option is requested by the client or not
bool DhcpHandler::IsOptionRequested(uint8_t option) {
    for (uint32_t i = 0; i < parameters_.size(); i++) {
        if (parameters_[i] == option)
            return true;
    }
    return false;
}

bool DhcpHandler::IsRouterOptionNeeded() {
    // If GW is not configured, dont include
    if (config_.gw_addr.is_unspecified())
        return false;

    // If router option is already included, nothing to do
    if (is_flag_set(DHCP_OPTION_ROUTER))
        return false;

    // When client requests Classless Static Routes option and this is
    // included in the response, Router option is not included (RFC3442)
    if (IsOptionRequested(DHCP_OPTION_CLASSLESS_ROUTE) &&
        is_flag_set(DHCP_OPTION_CLASSLESS_ROUTE))
        return false;

    return true;
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
    // if the option name is a number, use it as DHCP code
    // otherwise, use it as option name
    std::stringstream str(option);
    uint32_t code = 0;
    str >> code;
    if (code) return code;

    Dhcpv4NameCodeIter iter =
        g_dhcpv4_namecode_map.find(boost::to_lower_copy(option));
    return (iter == g_dhcpv4_namecode_map.end()) ? 0 : iter->second;
}

void DhcpHandler::DhcpTrace(const std::string &msg) const {
    DHCP_TRACE(Error, "VM " << config_.ip_addr.to_string() << " : " << msg);
}
