/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "vr_defs.h"
#include "cmn/agent_cmn.h"
#include "oper/route_common.h"
#include "pkt/pkt_init.h"
#include "services/dhcpv6_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dns_proto.h"
#include "services/services_sandesh.h"
#include "bind/bind_util.h"
#include "bind/xmpp_dns_agent.h"

#include <boost/assign/list_of.hpp>
using namespace boost::assign;

// since the DHCPv4 and DHCPv6 option codes mean different things
// and since we get only one set of DHCP options from the config or
// from Neutron, it is assumed that if the option code is a number,
// it represents DHCPv4 code and the code string for DHCPv6 and
// DHCPv4 do not overlap.
Dhcpv6NameCodeMap g_dhcpv6_namecode_map =
        map_list_of<std::string, uint32_t>
        ("v6-client-id", DHCPV6_OPTION_CLIENTID)
        ("v6-server-id", DHCPV6_OPTION_SERVERID)
        ("v6-ia-na", DHCPV6_OPTION_IA_NA)
        ("v6-ia-ta", DHCPV6_OPTION_IA_TA)
        ("v6-ia-addr", DHCPV6_OPTION_IAADDR)
        ("v6-oro", DHCPV6_OPTION_ORO)
        ("v6-preference", DHCPV6_OPTION_PREFERENCE)
        ("v6-elapsed-time", DHCPV6_OPTION_ELAPSED_TIME)
        ("v6-relay-msg", DHCPV6_OPTION_RELAY_MSG)
        ("v6-auth", DHCPV6_OPTION_AUTH)
        ("v6-unicast", DHCPV6_OPTION_UNICAST)
        ("v6-status-code", DHCPV6_OPTION_STATUS_CODE)
        ("v6-rapid-commit", DHCPV6_OPTION_RAPID_COMMIT)
        ("v6-user-class", DHCPV6_OPTION_USER_CLASS)
        ("v6-vendor-class", DHCPV6_OPTION_VENDOR_CLASS)
        ("v6-vendor-opts", DHCPV6_OPTION_VENDOR_OPTS)
        ("v6-interface-id", DHCPV6_OPTION_INTERFACE_ID)
        ("v6-reconf-msg",  DHCPV6_OPTION_RECONF_MSG)
        ("v6-reconf-accept", DHCPV6_OPTION_RECONF_ACCEPT)
        ("v6-sip-server-names", DHCPV6_OPTION_SIP_SERVER_D)
        ("v6-sip-server-addresses", DHCPV6_OPTION_SIP_SERVER_A)
        ("v6-name-servers", DHCPV6_OPTION_DNS_SERVERS)
        ("v6-domain-search", DHCPV6_OPTION_DOMAIN_LIST)
        ("v6-ia-pd", DHCPV6_OPTION_IA_PD)
        ("v6-ia-prefix", DHCPV6_OPTION_IAPREFIX)
        ("v6-nis-servers", DHCPV6_OPTION_NIS_SERVERS)
        ("v6-nisp-servers", DHCPV6_OPTION_NISP_SERVERS)
        ("v6-nis-domain-name", DHCPV6_OPTION_NIS_DOMAIN_NAME)
        ("v6-nisp-domain-name", DHCPV6_OPTION_NISP_DOMAIN_NAME)
        ("v6-sntp-servers", DHCPV6_OPTION_SNTP_SERVERS)
        ("v6-info-refresh-time", DHCPV6_OPTION_INFORMATION_REFRESH_TIME)
        ("v6-bcms-server-d", DHCPV6_OPTION_BCMCS_SERVER_D)
        ("v6-bcms-server-a", DHCPV6_OPTION_BCMCS_SERVER_A)
        ("v6-geoconf-civic", DHCPV6_OPTION_GEOCONF_CIVIC)
        ("v6-remote-id", DHCPV6_OPTION_REMOTE_ID)
        ("v6-subscriber-id", DHCPV6_OPTION_SUBSCRIBER_ID)
        ("v6-client-fqdn", DHCPV6_OPTION_CLIENT_FQDN)
        ("v6-pana-agent", DHCPV6_OPTION_PANA_AGENT)
        ("v6-posiz-timezone", DHCPV6_OPTION_NEW_POSIX_TIMEZONE)
        ("v6-tzdc-timezone", DHCPV6_OPTION_NEW_TZDB_TIMEZONE)
        ("v6-ero", DHCPV6_OPTION_ERO)
        ("v6-lq-query", DHCPV6_OPTION_LQ_QUERY)
        ("v6-client-data", DHCPV6_OPTION_CLIENT_DATA)
        ("v6-clt-time", DHCPV6_OPTION_CLT_TIME)
        ("v6-lq-relay-data", DHCPV6_OPTION_LQ_RELAY_DATA)
        ("v6-lq-client-link", DHCPV6_OPTION_LQ_CLIENT_LINK)
        ("mip6-hnidf", DHCPV6_OPTION_MIP6_HNIDF)
        ("mip6-vdinf", DHCPV6_OPTION_MIP6_VDINF)
        ("v6-lost", DHCPV6_OPTION_V6_LOST)
        ("v6-capwap-ac", DHCPV6_OPTION_CAPWAP_AC_V6)
        ("v6-relay-id", DHCPV6_OPTION_RELAY_ID)
        ("v6-address-mos", DHCPV6_OPTION_IPv6_Address_MoS)
        ("v6-fqdn-mos", DHCPV6_OPTION_IPv6_FQDN_MoS)
        ("v6-ntp-server", DHCPV6_OPTION_NTP_SERVER)
        ("v6-access-domain", DHCPV6_OPTION_V6_ACCESS_DOMAIN)
        ("v6-sip-ua-cs-list", DHCPV6_OPTION_SIP_UA_CS_LIST)
        ("v6-bootfile-url", DHCPV6_OPT_BOOTFILE_URL)
        ("v6-bootfile-param", DHCPV6_OPT_BOOTFILE_PARAM)
        ("v6-client-arch-type", DHCPV6_OPTION_CLIENT_ARCH_TYPE)
        ("v6-nii", DHCPV6_OPTION_NII)
        ("v6-geolocation", DHCPV6_OPTION_GEOLOCATION)
        ("v6-aftr-name", DHCPV6_OPTION_AFTR_NAME)
        ("v6-erp-local-domain-name", DHCPV6_OPTION_ERP_LOCAL_DOMAIN_NAME)
        ("v6-rsoo", DHCPV6_OPTION_RSOO)
        ("v6-pd-exclude", DHCPV6_OPTION_PD_EXCLUDE)
        ("v6-vss", DHCPV6_OPTION_VSS)
        ("mip6-idinf", DHCPV6_OPTION_MIP6_IDINF)
        ("mip6-udinf", DHCPV6_OPTION_MIP6_UDINF)
        ("mip6-hnp", DHCPV6_OPTION_MIP6_HNP)
        ("mip6-haa", DHCPV6_OPTION_MIP6_HAA)
        ("mip6-haf", DHCPV6_OPTION_MIP6_HAF)
        ("v6-rdnss-selection", DHCPV6_OPTION_RDNSS_SELECTION)
        ("v6-krb-principal-name", DHCPV6_OPTION_KRB_PRINCIPAL_NAME)
        ("v6-krb-realm-name", DHCPV6_OPTION_KRB_REALM_NAME)
        ("v6-krb-default-realm-name", DHCPV6_OPTION_KRB_DEFAULT_REALM_NAME)
        ("v6-krb-kdc", DHCPV6_OPTION_KRB_KDC)
        ("v6-client-linklayer-addr", DHCPV6_OPTION_CLIENT_LINKLAYER_ADDR)
        ("v6-link-address", DHCPV6_OPTION_LINK_ADDRESS)
        ("v6-radius", DHCPV6_OPTION_RADIUS)
        ("v6-sol-max-rt", DHCPV6_OPTION_SOL_MAX_RT)
        ("v6-inf-max-rt", DHCPV6_OPTION_INF_MAX_RT)
        ("v6-addrsel", DHCPV6_OPTION_ADDRSEL)
        ("v6-addrsel-table", DHCPV6_OPTION_ADDRSEL_TABLE)
        ("v6-pcp-server", DHCPV6_OPTION_V6_PCP_SERVER)
        ("v6-dhcpv4-msg", DHCPV6_OPTION_DHCPV4_MSG)
        ("v6-dhcpv4-o-dhcpv6-server", DHCPV6_OPTION_DHCP4_O_DHCP6_SERVER)
        ("v6-s46-rule", DHCPV6_OPTION_S46_RULE)
        ("v6-s46-br", DHCPV6_OPTION_S46_BR)
        ("v6-s46-dmr", DHCPV6_OPTION_S46_DMR)
        ("v6-s46-v4v6bind", DHCPV6_OPTION_S46_V4V6BIND)
        ("v6-s46-portparams", DHCPV6_OPTION_S46_PORTPARAMS)
        ("v6-s46-cont-mape", DHCPV6_OPTION_S46_CONT_MAPE)
        ("v6-s46-cont-mapt", DHCPV6_OPTION_S46_CONT_MAPT)
        ("v6-s46-cont-lw", DHCPV6_OPTION_S46_CONT_LW)
        ("v6-address-andsf", DHCPV6_OPTION_IPv6_ADDRESS_ANDSF);

Dhcpv6CategoryMap g_dhcpv6_category_map =
    map_list_of<uint32_t, Dhcpv6Handler::Dhcpv6OptionCategory>
        // the following are sent from Agent
        // (DHCPV6_OPTION_CLIENTID, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_SERVERID, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_IA_NA, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_IA_TA, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_IAADDR, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_ORO, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_PREFERENCE, Dhcpv6Handler::Byte)
        // (DHCPV6_OPTION_ELAPSED_TIME, Dhcpv6Handler::Uint16bit)
        // (DHCPV6_OPTION_RELAY_MSG, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_AUTH, Dhcpv6Handler::ByteArray)
        // (DHCPV6_OPTION_UNICAST, Dhcpv6Handler::OneIPv6)
        // (DHCPV6_OPTION_STATUS_CODE, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_RAPID_COMMIT, Dhcpv6Handler::NoData)
        (DHCPV6_OPTION_USER_CLASS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_VENDOR_CLASS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_VENDOR_OPTS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_INTERFACE_ID, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_RECONF_MSG, Dhcpv6Handler::Byte)
        (DHCPV6_OPTION_RECONF_ACCEPT, Dhcpv6Handler::NoData)
        (DHCPV6_OPTION_SIP_SERVER_D, Dhcpv6Handler::NameCompressionArray)
        (DHCPV6_OPTION_SIP_SERVER_A, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_DNS_SERVERS, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_DOMAIN_LIST, Dhcpv6Handler::NameCompressionArray)
        (DHCPV6_OPTION_IA_PD, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_IAPREFIX, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_NIS_SERVERS, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_NISP_SERVERS, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_NIS_DOMAIN_NAME, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_NISP_DOMAIN_NAME, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_SNTP_SERVERS, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_INFORMATION_REFRESH_TIME, Dhcpv6Handler::Uint32bit)
        (DHCPV6_OPTION_BCMCS_SERVER_D, Dhcpv6Handler::NameCompressionArray)
        (DHCPV6_OPTION_BCMCS_SERVER_A, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_GEOCONF_CIVIC, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_REMOTE_ID, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_SUBSCRIBER_ID, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_CLIENT_FQDN, Dhcpv6Handler::ByteNameCompression)
        (DHCPV6_OPTION_PANA_AGENT, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_NEW_POSIX_TIMEZONE, Dhcpv6Handler::String)
        (DHCPV6_OPTION_NEW_TZDB_TIMEZONE, Dhcpv6Handler::String)
        (DHCPV6_OPTION_ERO, Dhcpv6Handler::Uint16bitArray)
        (DHCPV6_OPTION_LQ_QUERY, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_CLIENT_DATA, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_CLT_TIME, Dhcpv6Handler::Uint32bit)
        (DHCPV6_OPTION_LQ_RELAY_DATA, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_LQ_CLIENT_LINK, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_MIP6_HNIDF, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_MIP6_VDINF, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_V6_LOST, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_CAPWAP_AC_V6, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_RELAY_ID, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_IPv6_Address_MoS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_IPv6_FQDN_MoS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_NTP_SERVER, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_V6_ACCESS_DOMAIN, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_SIP_UA_CS_LIST, Dhcpv6Handler::NameCompressionArray)
        (DHCPV6_OPT_BOOTFILE_URL, Dhcpv6Handler::String)
        (DHCPV6_OPT_BOOTFILE_PARAM, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_CLIENT_ARCH_TYPE, Dhcpv6Handler::Uint16bitArray)
        (DHCPV6_OPTION_NII, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_GEOLOCATION, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_AFTR_NAME, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_ERP_LOCAL_DOMAIN_NAME, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_RSOO, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_PD_EXCLUDE, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_VSS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_MIP6_IDINF, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_MIP6_UDINF, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_MIP6_HNP, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_MIP6_HAA, Dhcpv6Handler::OneIPv6)
        (DHCPV6_OPTION_MIP6_HAF, Dhcpv6Handler::NameCompression)
        (DHCPV6_OPTION_RDNSS_SELECTION, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_KRB_PRINCIPAL_NAME, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_KRB_REALM_NAME, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_KRB_DEFAULT_REALM_NAME, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_KRB_KDC, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_CLIENT_LINKLAYER_ADDR, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_LINK_ADDRESS, Dhcpv6Handler::OneIPv6)
        (DHCPV6_OPTION_RADIUS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_SOL_MAX_RT, Dhcpv6Handler::Uint32bit)
        (DHCPV6_OPTION_INF_MAX_RT, Dhcpv6Handler::Uint32bit)
        (DHCPV6_OPTION_ADDRSEL, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_ADDRSEL_TABLE, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_V6_PCP_SERVER, Dhcpv6Handler::OneIPv6Plus)
        (DHCPV6_OPTION_DHCPV4_MSG, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_DHCP4_O_DHCP6_SERVER, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_RULE, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_BR, Dhcpv6Handler::OneIPv6)
        (DHCPV6_OPTION_S46_DMR, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_V4V6BIND, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_PORTPARAMS, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_CONT_MAPE, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_CONT_MAPT, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_S46_CONT_LW, Dhcpv6Handler::ByteArray)
        (DHCPV6_OPTION_IPv6_ADDRESS_ANDSF, Dhcpv6Handler::OneIPv6Plus);

Dhcpv6Handler::Dhcpv6Handler(Agent *agent, boost::shared_ptr<PktInfo> info,
                             boost::asio::io_service &io)
        : ProtoHandler(agent, info, io), vm_itf_(NULL),
          msg_type_(DHCPV6_UNKNOWN), out_msg_type_(DHCPV6_UNKNOWN),
          rapid_commit_(false), reconfig_accept_(false),
          client_duid_len_(0), server_duid_len_(0),
          client_duid_(NULL), server_duid_(NULL) {
    memset(xid_, 0, sizeof(xid_));
    ipam_type_.ipam_dns_method = "none";
};

bool Dhcpv6Handler::Run() {
    dhcp_ = (Dhcpv6Hdr *) pkt_info_->data;
    // request_.UpdateData(dhcp_->xid, ntohs(dhcp_->flags), dhcp_->chaddr);
    Dhcpv6Proto *dhcp_proto = agent()->dhcpv6_proto();
    Interface *itf =
        agent()->interface_table()->FindInterface(GetInterfaceIndex());
    if (itf == NULL) {
        dhcp_proto->IncrStatsError();
        DHCPV6_TRACE(Error, "Received DHCP packet on invalid interface : "
                     << GetInterfaceIndex());
        return true;
    }

    if (itf->type() != Interface::VM_INTERFACE) {
        dhcp_proto->IncrStatsError();
        DHCPV6_TRACE(Error, "Received DHCP packet on non VM port interface : "
                     << GetInterfaceIndex());
        return true;
    }
    vm_itf_ = static_cast<VmInterface *>(itf);
    if (!vm_itf_->layer3_forwarding()) {
        dhcp_proto->IncrStatsError();
        DHCPV6_TRACE(Error, "DHCP request on VM port with disabled ip service: "
                     << GetInterfaceIndex());
        return true;
    }

    msg_type_ = dhcp_->type;
    memcpy(xid_, dhcp_->xid, 3);
    // options length = pkt length - size of headers
    int16_t options_len = pkt_info_->len -
                          (pkt_info_->data - (uint8_t *)pkt_info_->eth)
                          - DHCPV6_FIXED_LEN;
    ReadOptions(options_len);

    switch (msg_type_) {
        case DHCPV6_SOLICIT:
            out_msg_type_ = DHCPV6_ADVERTISE;
            dhcp_proto->IncrStatsSolicit();
            DHCPV6_TRACE(Trace, "DHCP solicit received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_REQUEST:
            out_msg_type_ = DHCPV6_REPLY;
            dhcp_proto->IncrStatsRequest();
            DHCPV6_TRACE(Trace, "DHCP request received on interface : "
                         << vm_itf_->name());
            break;

        // TODO: following messages require special handling
        case DHCPV6_CONFIRM:
            out_msg_type_ = DHCPV6_REPLY;
            dhcp_proto->IncrStatsConfirm();
            DHCPV6_TRACE(Trace, "DHCP confirm received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_RENEW:
            out_msg_type_ = DHCPV6_REPLY;
            dhcp_proto->IncrStatsRenew();
            DHCPV6_TRACE(Trace, "DHCP renew received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_REBIND:
            out_msg_type_ = DHCPV6_REPLY;
            dhcp_proto->IncrStatsRebind();
            DHCPV6_TRACE(Trace, "DHCP bind received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_RELEASE:
            out_msg_type_ = DHCPV6_REPLY;
            dhcp_proto->IncrStatsRelease();
            DHCPV6_TRACE(Trace, "DHCP release received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_DECLINE:
            out_msg_type_ = DHCPV6_REPLY;
            dhcp_proto->IncrStatsDecline();
            DHCPV6_TRACE(Trace, "DHCP decline received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_RECONFIGURE:
            dhcp_proto->IncrStatsReconfigure();
            DHCPV6_TRACE(Trace, "DHCP reconfigure received on interface : "
                         << vm_itf_->name());
            break;

        case DHCPV6_INFORMATION_REQUEST:
            dhcp_proto->IncrStatsInformationRequest();
            DHCPV6_TRACE(Trace,
                         "DHCP information request received on interface : "
                         << vm_itf_->name());
            break;

        default:
            DHCPV6_TRACE(Trace, "DHCP message " << msg_type_ <<
                         " received on interface : " << vm_itf_->name() <<
                       "; ignoring");
            dhcp_proto->IncrStatsError();
            return true;
    }

    if (FindLeaseData()) {
        SendDhcpResponse();
        DHCPV6_TRACE(Trace, "DHCP response sent; message = " << 
                     ServicesSandesh::Dhcpv6MsgType(out_msg_type_) << 
                     "; ip = " << config_.ip_addr.to_string());
    }

    return true;
}

// read DHCP options in the incoming packet
void Dhcpv6Handler::ReadOptions(int16_t opt_rem_len) {
    Dhcpv6Options *opt = dhcp_->options;
    // parse thru the option fields
    while (opt_rem_len > 0) {
        uint16_t option_code = ntohs(opt->code);
        uint16_t option_len = ntohs(opt->len);
        switch (option_code) {
            case DHCPV6_OPTION_CLIENTID:
                client_duid_len_ = option_len;
                client_duid_.reset(new uint8_t[client_duid_len_]);
                memcpy(client_duid_.get(), opt->data, client_duid_len_);
                break;

            case DHCPV6_OPTION_SERVERID:
                server_duid_len_ = option_len;
                server_duid_.reset(new uint8_t[server_duid_len_]);
                memcpy(server_duid_.get(), opt->data, server_duid_len_);
                break;

            case DHCPV6_OPTION_IA_NA:
            case DHCPV6_OPTION_IA_TA:
                ReadIA(opt->data, option_len, option_code);
                break;

            case DHCPV6_OPTION_RAPID_COMMIT:
                rapid_commit_ = true;
                break;

            case DHCPV6_OPTION_RECONF_ACCEPT:
                reconfig_accept_ = true;
                break;

            case DHCPV6_OPTION_ORO: // list of requested options
            case DHCPV6_OPTION_PREFERENCE: // server preference, sent by server
            case DHCPV6_OPTION_ELAPSED_TIME: // time client has been trying
            case DHCPV6_OPTION_RELAY_MSG: // relay DHCP types are not handled
            case DHCPV6_OPTION_AUTH: // authentication
            case DHCPV6_OPTION_UNICAST: // server option, sent to get ucast msgs
            case DHCPV6_OPTION_USER_CLASS: // type of user
            case DHCPV6_OPTION_VENDOR_CLASS: // vendor
            case DHCPV6_OPTION_VENDOR_OPTS:
            case DHCPV6_OPTION_INTERFACE_ID: // used by relay agent
            case DHCPV6_OPTION_RECONF_MSG: // can only appear in reconfigure msg
                // ignore these options
                break;

            default:
                break;
        }
        opt_rem_len -= (4 + option_len);
        opt = (Dhcpv6Options *)((uint8_t *)opt + 4 + option_len);
    }
}

bool Dhcpv6Handler::ReadIA(uint8_t *ptr, uint16_t len, uint16_t code) {
    Dhcpv6Ia *iana = (Dhcpv6Ia *)ptr;
    Dhcpv6IaAddr *addr = NULL;
    int16_t iana_rem_len = len - sizeof(Dhcpv6Ia);
    Dhcpv6Options *iana_option = (Dhcpv6Options *)(ptr + sizeof(Dhcpv6Ia));
    while (iana_rem_len > 0) {
        uint16_t iana_option_code = ntohs(iana_option->code);
        uint16_t iana_option_len = ntohs(iana_option->len);
        switch (iana_option_code) {
            case DHCPV6_OPTION_IAADDR: {
                addr = (Dhcpv6IaAddr *)iana_option->data;
                break;
            }

            case DHCPV6_OPTION_STATUS_CODE: {
                uint16_t *s_ptr = reinterpret_cast<uint16_t *>
                                      (iana_option->data);
                uint16_t status = ntohs(*s_ptr);
                if (status != DHCPV6_SUCCESS) {
                    std::string msg((const char *)iana_option->data + 2,
                                    iana_option_len - 2);
                    DHCPV6_TRACE(Trace, "DHCP message with error status : " <<
                                 status <<  " error msg : " << msg <<
                                 "; received on interface : " <<
                                 vm_itf_->name() << "; ignoring");
                }
                return false;
            }

            default:
                break;
        }
        iana_rem_len -= (4 + iana_option_len);
        iana_option = (Dhcpv6Options *)((uint8_t *)iana_option + 4 + iana_option_len);
    }

    if (code == DHCPV6_OPTION_IA_NA)
        iana_.push_back(Dhcpv6IaData(iana, addr));
    else
        iata_.push_back(Dhcpv6IaData(iana, addr));
    return true;
}

void Dhcpv6Handler::FillDhcpInfo(Ip6Address &addr, int plen,
                                 Ip6Address &gw, Ip6Address &dns) {
    config_.ip_addr = addr;
    config_.plen = plen;
    config_.subnet_mask = GetIp6SubnetAddress(addr, plen);
    config_.gw_addr = gw;
    config_.dns_addr = dns;
}

bool Dhcpv6Handler::FindLeaseData() {
    Ip6Address ip = vm_itf_->ip6_addr();
    FindDomainName();
    if (vm_itf_->ipv6_active()) {
        if (vm_itf_->fabric_port()) {
            // TODO
            agent()->dhcpv6_proto()->IncrStatsError();
            DHCPV6_TRACE(Error, "DHCP fabric port request failed for : "
                         << ip.to_string());
            return false;
        }

        const std::vector<VnIpam> &ipam = vm_itf_->vn()->GetVnIpam();
        for (uint32_t i = 0; i < ipam.size(); ++i) {
            if (!ipam[i].IsV6()) {
                continue;
            }
            if (IsIp6SubnetMember(ip, ipam[i].ip_prefix.to_v6(), 
                                  ipam[i].plen)) {
                Ip6Address default_gw = ipam[i].default_gw.to_v6();
                FillDhcpInfo(ip, ipam[i].plen, default_gw, default_gw);
                return true;
            }
        }
    }

    // We dont have the config yet; give a short lease
    config_.preferred_time = DHCPV6_SHORTLEASE_TIME;
    config_.valid_time = 2 * DHCPV6_SHORTLEASE_TIME;
    // Give address received from Nova
    Ip6Address empty;
    FillDhcpInfo(ip, 128, empty, empty);
    DHCPV6_TRACE(Trace, "DHCP giving short lease given for : " << ip.to_string()
                 << "; IPv6 not active in Agent");
    return true;
}

void Dhcpv6Handler::FindDomainName() {
    if (config_.preferred_time != (uint32_t) -1)
        return;

    if (!vm_itf_->vn() || 
        !vm_itf_->vn()->GetIpamData(vm_itf_->ip6_addr(), &ipam_name_,
                                    &ipam_type_)) {
        DHCPV6_TRACE(Trace, "Ipam data not found; VM = " << vm_itf_->name());
        return;
    }

    if (ipam_type_.ipam_dns_method != "virtual-dns-server" ||
        !agent()->domain_config_table()->GetVDns(ipam_type_.ipam_dns_server.
                                          virtual_dns_server_name, &vdns_type_))
        return;

    config_.domain_name_ = vdns_type_.domain_name;
}

// Add option taking no data (length = 0)
uint16_t Dhcpv6Handler::AddNoDataOption(uint32_t option, uint16_t opt_len) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    return opt_len;
}

// Add option taking a byte from input
uint16_t Dhcpv6Handler::AddByteOption(uint32_t option, uint16_t opt_len,
                                      const std::string &input) {
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail() || data > 0xFF) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << " data : " <<
                     input << "; invalid data");
    } else {
        uint8_t byte = (uint8_t)data;
        Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
        opt->WriteData(option, 1, &byte, &opt_len);
    }
    return opt_len;
}

// Add option taking array of bytes from input
uint16_t Dhcpv6Handler::AddByteArrayOption(uint32_t option, uint16_t opt_len,
                                           const std::string &input) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
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
    if (!opt->GetLen() || !value.eof()) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << "; data missing");
        return opt_len - opt->GetLen() - 4;
    }

    return opt_len;
}

// Add option taking string from input
uint16_t Dhcpv6Handler::AddStringOption(uint32_t option, uint16_t opt_len,
                                        const std::string &input) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, input.length(), input.c_str(), &opt_len);
    return opt_len;
}

// Add option taking integer from input
uint16_t Dhcpv6Handler::AddIntegerOption(uint32_t option, uint16_t opt_len,
                                         const std::string &input) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail()) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << " data : " <<
                     input << "; invalid data");
    } else {
        data = htonl(data);
        opt->WriteData(option, 4, &data, &opt_len);
    }
    return opt_len;
}

// Add option taking array of short from input
uint16_t Dhcpv6Handler::AddShortArrayOption(uint32_t option, uint16_t opt_len,
                                            const std::string &input,
                                            bool array) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
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
    if (!opt->GetLen() || !value.eof()) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << " data : " <<
                     input << "; invalid data");
        return opt_len - opt->GetLen() - 4;
    }

    return opt_len;
}

uint16_t Dhcpv6Handler::AddIpOption(uint32_t option, uint16_t opt_len,
                                    const std::string &input, bool list) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string ipstr;
        value >> ipstr;
        opt_len = AddIP(opt, opt_len, ipstr);
        if (!list) break;
    }

    if (option == DHCPV6_OPTION_DNS_SERVERS) {
        // Add DNS servers from the IPAM dns method and server config also
        opt_len = AddDnsServers(opt, opt_len);
    }

    // check that atleast one IP address is added
    if (opt->GetLen() < 16) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << "; data missing");
        return opt_len - opt->GetLen() - 4;
    }

    return opt_len;
}

// Add an IP address to the option
uint16_t Dhcpv6Handler::AddIP(Dhcpv6Options *opt, uint16_t opt_len,
                              const std::string &input) {
    boost::system::error_code ec;
    Ip6Address ip = Ip6Address::from_string(input, ec);
    if (!ec.value()) {
        opt->AppendData(16, ip.to_bytes().data(), &opt_len);
    } else {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << opt->code << " for VM " <<
                     config_.ip_addr.to_string() << "; has to be IP address");
    }
    return opt_len;
}

uint16_t Dhcpv6Handler::AddCompressedNameOption(uint32_t option,
                                                uint16_t opt_len,
                                                const std::string &input,
                                                bool list) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);
    std::stringstream value(input);
    while (value.good()) {
        std::string str;
        value >> str;
        if (str.size()) {
            opt_len = AddCompressedName(opt, opt_len, str);
            if (!list) break;
        }
    }

    if (!opt->GetLen()) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << "; invalid data");
        return opt_len - opt->GetLen() - 4;
    }

    return opt_len;
}

uint16_t Dhcpv6Handler::AddByteCompressedNameOption(uint32_t option,
                                                    uint16_t opt_len,
                                                    const std::string &input) {
    Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
    opt->WriteData(option, 0, NULL, &opt_len);

    std::stringstream value(input);
    uint32_t data = 0;
    value >> data;
    if (value.bad() || value.fail() || data > 0xFF) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << " data : " <<
                     input << "; invalid data");
        return opt_len - opt->GetLen() - 4;
    } else {
        uint8_t byte = (uint8_t)data;
        Dhcpv6Options *opt = GetNextOptionPtr(opt_len);
        opt->WriteData(option, 1, &byte, &opt_len);
    }

    std::string str;
    value >> str;
    if (value.bad() || value.fail() || !str.size()) {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option << " for VM " <<
                     config_.ip_addr.to_string() << "; invalid data");
        return opt_len - opt->GetLen() - 4;
    }
    return AddCompressedName(opt, opt_len, str);
}

uint16_t Dhcpv6Handler::AddCompressedName(Dhcpv6Options *opt, uint16_t opt_len,
                                          const std::string &input) {
    uint8_t name[input.size() * 2 + 2];
    uint16_t len = 0;
    BindUtil::AddName(name, input, 0, 0, len);
    opt->AppendData(len, name, &opt_len);
    return opt_len;
}

// Add an DNS server addresses from IPAM to the option
uint16_t Dhcpv6Handler::AddDnsServers(Dhcpv6Options *opt, uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "default-dns-server" ||
        ipam_type_.ipam_dns_method == "virtual-dns-server" ||
        ipam_type_.ipam_dns_method == "") {
        if (!config_.dns_addr.is_unspecified()) {
            opt->AppendData(16, config_.dns_addr.to_bytes().data(), &opt_len);
        }
    } else if (ipam_type_.ipam_dns_method == "tenant-dns-server") {
        for (unsigned int i = 0; i < ipam_type_.ipam_dns_server.
             tenant_dns_server_address.ip_address.size(); ++i) {
            // AddIP adds only IPv6 addresses
            opt_len = AddIP(opt, opt_len,
                            ipam_type_.ipam_dns_server.
                            tenant_dns_server_address.ip_address[i]);
        }
    }
    return opt_len;
}

// Add domain name from IPAM to the option
uint16_t Dhcpv6Handler::AddDomainNameOption(Dhcpv6Options *opt, uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        if (config_.domain_name_.size()) {
            // encode the domain name in the dns encoding format
            uint8_t domain_name[config_.domain_name_.size() * 2 + 2];
            uint16_t len = 0;
            BindUtil::AddName(domain_name, config_.domain_name_, 0, 0, len);
            opt->WriteData(DHCPV6_OPTION_DOMAIN_LIST, len,
                           domain_name, &opt_len);
        }
    }
    return opt_len;
}

uint16_t Dhcpv6Handler::AddConfigDhcpOptions(uint16_t opt_len) {
    std::vector<autogen::DhcpOptionType> options;
    if (vm_itf_->GetInterfaceDhcpOptions(&options))
        opt_len = AddDhcpOptions(opt_len, options);

    if (vm_itf_->GetSubnetDhcpOptions(&options, true))
        opt_len = AddDhcpOptions(opt_len, options);

    if (vm_itf_->GetIpamDhcpOptions(&options, true))
        opt_len = AddDhcpOptions(opt_len, options);

    return opt_len;
}

uint16_t Dhcpv6Handler::AddDhcpOptions(
                        uint16_t opt_len,
                        std::vector<autogen::DhcpOptionType> &options) {
    for (unsigned int i = 0; i < options.size(); ++i) {
        // if the option name is a number, it is considered DHCPv4 option
        std::stringstream str(options[i].dhcp_option_name);
        uint32_t option =
                  OptionCode(boost::to_lower_copy(options[i].dhcp_option_name));
        if (!option) {
            DHCPV6_TRACE(Trace, "Invalid DHCP option : " <<
                       options[i].dhcp_option_name << " for VM " <<
                       config_.ip_addr.to_string());
            continue;
        }

        // if option is already set in the response (from higher level), ignore
        if (is_flag_set(option))
            continue;

        uint16_t old_opt_len = opt_len;
        Dhcpv6OptionCategory category = OptionCategory(option);

        switch(category) {
            case None:
                break;

            case NoData:
                opt_len = AddNoDataOption(option, opt_len);
                break;

            case Byte:
                opt_len = AddByteOption(option, opt_len,
                                        options[i].dhcp_option_value);
                break;

            case ByteArray:
                opt_len = AddByteArrayOption(option, opt_len,
                                             options[i].dhcp_option_value);
                break;

            case String:
                opt_len = AddStringOption(option, opt_len,
                                          options[i].dhcp_option_value);
                break;

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

            case OneIPv6:
                opt_len = AddIpOption(option, opt_len,
                                      options[i].dhcp_option_value,
                                      false);
                break;

            case OneIPv6Plus:
                opt_len = AddIpOption(option, opt_len,
                                      options[i].dhcp_option_value,
                                      true);
                break;

            case NameCompression:
                opt_len = AddCompressedNameOption(option, opt_len,
                                                  options[i].dhcp_option_value,
                                                  false);
                break;

            case ByteNameCompression:
                opt_len = AddByteCompressedNameOption(
                            option, opt_len, options[i].dhcp_option_value);
                break;

            case NameCompressionArray:
                opt_len = AddCompressedNameOption(option, opt_len,
                                                  options[i].dhcp_option_value,
                                                  true);
                break;

            default:
                DHCPV6_TRACE(Error, "Unsupported DHCP option : " +
                             options[i].dhcp_option_name);
                break;
        }

        if (opt_len != old_opt_len)
            set_flag(option);
    }

    return opt_len;
}

uint16_t Dhcpv6Handler::FillDhcpv6Hdr() {
    Dhcpv6Proto *dhcp_proto = agent()->dhcpv6_proto();

    dhcp_->type = out_msg_type_;
    memcpy(dhcp_->xid, xid_, 3);

    uint16_t opt_len = 0;
    Dhcpv6Options *opt = dhcp_->options;

    // server duid
    opt->WriteData(DHCPV6_OPTION_SERVERID, sizeof(Dhcpv6Proto::Duid),
                   (void *)dhcp_proto->server_duid(), &opt_len);

    // client duid
    if (client_duid_len_) {
        opt = GetNextOptionPtr(opt_len);
        opt->WriteData(DHCPV6_OPTION_CLIENTID, client_duid_len_,
                       (void *)client_duid_.get(), &opt_len);
    }

    // IA for non-temporary address
    // TODO : we currently give one IPv6 IP per interface; update when we
    // have to handle multiple iana / iata in a single request
    for (std::vector<Dhcpv6IaData>::iterator it = iana_.begin();
         it != iana_.end(); ++it) {
        opt = GetNextOptionPtr(opt_len);
        WriteIaOption(opt, it->ia, opt_len);
    }

    for (std::vector<Dhcpv6IaData>::iterator it = iata_.begin();
         it != iata_.end(); ++it) {
        opt = GetNextOptionPtr(opt_len);
        WriteIaOption(opt, it->ia, opt_len);
    }

    // Add dhcp options coming from Config
    opt_len = AddConfigDhcpOptions(opt_len);

    // GW doesnt come in DHCPV6, it should come via router advertisement

    if (!is_flag_set(DHCPV6_OPTION_DNS_SERVERS)) {
        opt = GetNextOptionPtr(opt_len);
        opt->WriteData(DHCPV6_OPTION_DNS_SERVERS, 0, NULL, &opt_len);
        uint16_t old_opt_len = opt_len;
        opt_len = AddDnsServers(opt, opt_len);
        if (opt_len == old_opt_len) opt_len -= 4;
    }

    if (!is_flag_set(DHCPV6_OPTION_DOMAIN_LIST)) {
        opt = GetNextOptionPtr(opt_len);
        opt_len = AddDomainNameOption(opt, opt_len);
    }

    return (DHCPV6_FIXED_LEN + opt_len);
}

void Dhcpv6Handler::WriteIaOption(Dhcpv6Options *opt,
                                  const Dhcpv6Ia &ia,
                                  uint16_t &optlen) {
    Dhcpv6IaAddr ia_addr(config_.ip_addr.to_bytes().data(),
                         config_.preferred_time,
                         config_.valid_time);

    opt->WriteData(DHCPV6_OPTION_IA_NA, sizeof(Dhcpv6Ia), (void *)&ia, &optlen);
    Dhcpv6Options *ia_addr_opt = GetNextOptionPtr(optlen);
    ia_addr_opt->WriteData(DHCPV6_OPTION_IAADDR, sizeof(Dhcpv6IaAddr),
                           (void *)&ia_addr, &optlen);
    opt->AddLen(sizeof(Dhcpv6IaAddr) + 4);
}

uint16_t Dhcpv6Handler::FillDhcpResponse(unsigned char *dest_mac,
                                         Ip6Address src_ip, Ip6Address dest_ip) {
    pkt_info_->eth = (ethhdr *)(pkt_info_->pkt);
    EthHdr(agent()->vhost_interface()->mac().ether_addr_octet,
           dest_mac, ETHERTYPE_IPV6);
    uint16_t header_len = sizeof(ethhdr);
    if (vm_itf_->vlan_id() != VmInterface::kInvalidVlanId) {
        // cfi and priority are zero
        VlanHdr(pkt_info_->pkt + 12, vm_itf_->vlan_id());
        header_len += sizeof(vlanhdr);
    }

    pkt_info_->ip6 = (ip6_hdr *)(pkt_info_->pkt + header_len);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip6 + 1);
    dhcp_ = (Dhcpv6Hdr *)(pkt_info_->transp.udp + 1);

    uint16_t len = FillDhcpv6Hdr();
    len += sizeof(udphdr);
    UdpHdr(len, src_ip.to_bytes().data(), DHCPV6_SERVER_PORT,
           dest_ip.to_bytes().data(), DHCPV6_CLIENT_PORT, IPPROTO_UDP);
    Ip6Hdr(pkt_info_->ip6, len, IPPROTO_UDP, 64, src_ip.to_bytes().data(),
           dest_ip.to_bytes().data());
    len += sizeof(ip6_hdr);

    pkt_info_->set_len(len + header_len);
    return pkt_info_->packet_buffer()->data_len();
}

void Dhcpv6Handler::SendDhcpResponse() {
    unsigned char dest_mac[ETH_ALEN];
    memcpy(dest_mac, pkt_info_->eth->h_source, ETH_ALEN);

    UpdateStats();
    FillDhcpResponse(dest_mac, config_.gw_addr,
                                    pkt_info_->ip_saddr.to_v6());
    Send(GetInterfaceIndex(), pkt_info_->vrf,
         AgentHdr::TX_SWITCH, PktHandler::DHCPV6);
}

void Dhcpv6Handler::UpdateStats() {
    Dhcpv6Proto *dhcp_proto = agent()->dhcpv6_proto();
    (out_msg_type_ == DHCPV6_ADVERTISE) ? dhcp_proto->IncrStatsAdvertise() :
                                          dhcp_proto->IncrStatsReply();
}

Dhcpv6Handler::Dhcpv6OptionCategory
Dhcpv6Handler::OptionCategory(uint32_t option) const {
    Dhcpv6CategoryIter iter = g_dhcpv6_category_map.find(option);
    if (iter == g_dhcpv6_category_map.end())
        return None;
    return iter->second;
}

uint32_t Dhcpv6Handler::OptionCode(const std::string &option) const {
    Dhcpv6NameCodeIter iter = g_dhcpv6_namecode_map.find(option);
    if (iter == g_dhcpv6_namecode_map.end())
        return 0;
    return iter->second;
}
