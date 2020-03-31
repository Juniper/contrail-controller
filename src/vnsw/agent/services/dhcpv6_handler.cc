/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdint.h>
#include "base/os.h"
#include "base/address_util.h"
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
#include <boost/scoped_array.hpp>
using namespace boost::assign;

// since the DHCPv4 and DHCPv6 option codes mean different things
// and since we get only one set of DHCP options from the config or
// from Neutron, it is assumed that if the option code is a number,
// it represents DHCPv4 code and the code string for DHCPv6 and
// DHCPv4 do not overlap.
Dhcpv6NameCodeMap g_dhcpv6_namecode_map = {
    {"v6-client-id", DHCPV6_OPTION_CLIENTID},
    {"v6-server-id", DHCPV6_OPTION_SERVERID},
    {"v6-ia-na", DHCPV6_OPTION_IA_NA},
    {"v6-ia-ta", DHCPV6_OPTION_IA_TA},
    {"v6-ia-addr", DHCPV6_OPTION_IAADDR},
    {"v6-oro", DHCPV6_OPTION_ORO},
    {"v6-preference", DHCPV6_OPTION_PREFERENCE},
    {"v6-elapsed-time", DHCPV6_OPTION_ELAPSED_TIME},
    {"v6-relay-msg", DHCPV6_OPTION_RELAY_MSG},
    {"v6-auth", DHCPV6_OPTION_AUTH},
    {"v6-unicast", DHCPV6_OPTION_UNICAST},
    {"v6-status-code", DHCPV6_OPTION_STATUS_CODE},
    {"v6-rapid-commit", DHCPV6_OPTION_RAPID_COMMIT},
    {"v6-user-class", DHCPV6_OPTION_USER_CLASS},
    {"v6-vendor-class", DHCPV6_OPTION_VENDOR_CLASS},
    {"v6-vendor-opts", DHCPV6_OPTION_VENDOR_OPTS},
    {"v6-interface-id", DHCPV6_OPTION_INTERFACE_ID},
    {"v6-reconf-msg", DHCPV6_OPTION_RECONF_MSG},
    {"v6-reconf-accept", DHCPV6_OPTION_RECONF_ACCEPT},
    {"v6-sip-server-names", DHCPV6_OPTION_SIP_SERVER_D},
    {"v6-sip-server-addresses", DHCPV6_OPTION_SIP_SERVER_A},
    {"v6-name-servers", DHCPV6_OPTION_DNS_SERVERS},
    {"v6-domain-search", DHCPV6_OPTION_DOMAIN_LIST},
    {"v6-ia-pd", DHCPV6_OPTION_IA_PD},
    {"v6-ia-prefix", DHCPV6_OPTION_IAPREFIX},
    {"v6-nis-servers", DHCPV6_OPTION_NIS_SERVERS},
    {"v6-nisp-servers", DHCPV6_OPTION_NISP_SERVERS},
    {"v6-nis-domain-name", DHCPV6_OPTION_NIS_DOMAIN_NAME},
    {"v6-nisp-domain-name", DHCPV6_OPTION_NISP_DOMAIN_NAME},
    {"v6-sntp-servers", DHCPV6_OPTION_SNTP_SERVERS},
    {"v6-info-refresh-time", DHCPV6_OPTION_INFORMATION_REFRESH_TIME},
    {"v6-bcms-server-d", DHCPV6_OPTION_BCMCS_SERVER_D},
    {"v6-bcms-server-a", DHCPV6_OPTION_BCMCS_SERVER_A},
    {"v6-geoconf-civic", DHCPV6_OPTION_GEOCONF_CIVIC},
    {"v6-remote-id", DHCPV6_OPTION_REMOTE_ID},
    {"v6-subscriber-id", DHCPV6_OPTION_SUBSCRIBER_ID},
    {"v6-client-fqdn", DHCPV6_OPTION_CLIENT_FQDN},
    {"v6-pana-agent", DHCPV6_OPTION_PANA_AGENT},
    {"v6-posiz-timezone", DHCPV6_OPTION_NEW_POSIX_TIMEZONE},
    {"v6-tzdc-timezone", DHCPV6_OPTION_NEW_TZDB_TIMEZONE},
    {"v6-ero", DHCPV6_OPTION_ERO},
    {"v6-lq-query", DHCPV6_OPTION_LQ_QUERY},
    {"v6-client-data", DHCPV6_OPTION_CLIENT_DATA},
    {"v6-clt-time", DHCPV6_OPTION_CLT_TIME},
    {"v6-lq-relay-data", DHCPV6_OPTION_LQ_RELAY_DATA},
    {"v6-lq-client-link", DHCPV6_OPTION_LQ_CLIENT_LINK},
    {"mip6-hnidf", DHCPV6_OPTION_MIP6_HNIDF},
    {"mip6-vdinf", DHCPV6_OPTION_MIP6_VDINF},
    {"v6-lost", DHCPV6_OPTION_V6_LOST},
    {"v6-capwap-ac", DHCPV6_OPTION_CAPWAP_AC_V6},
    {"v6-relay-id", DHCPV6_OPTION_RELAY_ID},
    {"v6-address-mos", DHCPV6_OPTION_IPv6_Address_MoS},
    {"v6-fqdn-mos", DHCPV6_OPTION_IPv6_FQDN_MoS},
    {"v6-ntp-server", DHCPV6_OPTION_NTP_SERVER},
    {"v6-access-domain", DHCPV6_OPTION_V6_ACCESS_DOMAIN},
    {"v6-sip-ua-cs-list", DHCPV6_OPTION_SIP_UA_CS_LIST},
    {"v6-bootfile-url", DHCPV6_OPT_BOOTFILE_URL},
    {"v6-bootfile-param", DHCPV6_OPT_BOOTFILE_PARAM},
    {"v6-client-arch-type", DHCPV6_OPTION_CLIENT_ARCH_TYPE},
    {"v6-nii", DHCPV6_OPTION_NII},
    {"v6-geolocation", DHCPV6_OPTION_GEOLOCATION},
    {"v6-aftr-name", DHCPV6_OPTION_AFTR_NAME},
    {"v6-erp-local-domain-name", DHCPV6_OPTION_ERP_LOCAL_DOMAIN_NAME},
    {"v6-rsoo", DHCPV6_OPTION_RSOO},
    {"v6-pd-exclude", DHCPV6_OPTION_PD_EXCLUDE},
    {"v6-vss", DHCPV6_OPTION_VSS},
    {"mip6-idinf", DHCPV6_OPTION_MIP6_IDINF},
    {"mip6-udinf", DHCPV6_OPTION_MIP6_UDINF},
    {"mip6-hnp", DHCPV6_OPTION_MIP6_HNP},
    {"mip6-haa", DHCPV6_OPTION_MIP6_HAA},
    {"mip6-haf", DHCPV6_OPTION_MIP6_HAF},
    {"v6-rdnss-selection", DHCPV6_OPTION_RDNSS_SELECTION},
    {"v6-krb-principal-name", DHCPV6_OPTION_KRB_PRINCIPAL_NAME},
    {"v6-krb-realm-name", DHCPV6_OPTION_KRB_REALM_NAME},
    {"v6-krb-default-realm-name", DHCPV6_OPTION_KRB_DEFAULT_REALM_NAME},
    {"v6-krb-kdc", DHCPV6_OPTION_KRB_KDC},
    {"v6-client-linklayer-addr", DHCPV6_OPTION_CLIENT_LINKLAYER_ADDR},
    {"v6-link-address", DHCPV6_OPTION_LINK_ADDRESS},
    {"v6-radius", DHCPV6_OPTION_RADIUS},
    {"v6-sol-max-rt", DHCPV6_OPTION_SOL_MAX_RT},
    {"v6-inf-max-rt", DHCPV6_OPTION_INF_MAX_RT},
    {"v6-addrsel", DHCPV6_OPTION_ADDRSEL},
    {"v6-addrsel-table", DHCPV6_OPTION_ADDRSEL_TABLE},
    {"v6-pcp-server", DHCPV6_OPTION_V6_PCP_SERVER},
    {"v6-dhcpv4-msg", DHCPV6_OPTION_DHCPV4_MSG},
    {"v6-dhcpv4-o-dhcpv6-server", DHCPV6_OPTION_DHCP4_O_DHCP6_SERVER},
    {"v6-s46-rule", DHCPV6_OPTION_S46_RULE},
    {"v6-s46-br", DHCPV6_OPTION_S46_BR},
    {"v6-s46-dmr", DHCPV6_OPTION_S46_DMR},
    {"v6-s46-v4v6bind", DHCPV6_OPTION_S46_V4V6BIND},
    {"v6-s46-portparams", DHCPV6_OPTION_S46_PORTPARAMS},
    {"v6-s46-cont-mape", DHCPV6_OPTION_S46_CONT_MAPE},
    {"v6-s46-cont-mapt", DHCPV6_OPTION_S46_CONT_MAPT},
    {"v6-s46-cont-lw", DHCPV6_OPTION_S46_CONT_LW},
    {"v6-address-andsf", DHCPV6_OPTION_IPv6_ADDRESS_ANDSF}};

Dhcpv6CategoryMap g_dhcpv6_category_map = {
    {DHCPV6_OPTION_CLIENTID, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_SERVERID, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_IA_NA, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_IA_TA, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_IAADDR, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_ORO, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_PREFERENCE, Dhcpv6Handler::Byte},
    // {DHCPV6_OPTION_ELAPSED_TIME, Dhcpv6Handler::Uint16bit},
    // {DHCPV6_OPTION_RELAY_MSG, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_AUTH, Dhcpv6Handler::ByteArray},
    // {DHCPV6_OPTION_UNICAST, Dhcpv6Handler::OneIPv6},
    // {DHCPV6_OPTION_STATUS_CODE, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_RAPID_COMMIT, Dhcpv6Handler::NoData},
    {DHCPV6_OPTION_USER_CLASS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_VENDOR_CLASS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_VENDOR_OPTS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_INTERFACE_ID, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_RECONF_MSG, Dhcpv6Handler::Byte},
    {DHCPV6_OPTION_RECONF_ACCEPT, Dhcpv6Handler::NoData},
    {DHCPV6_OPTION_SIP_SERVER_D, Dhcpv6Handler::NameCompressionArray},
    {DHCPV6_OPTION_SIP_SERVER_A, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_DNS_SERVERS, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_DOMAIN_LIST, Dhcpv6Handler::NameCompressionArray},
    {DHCPV6_OPTION_IA_PD, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_IAPREFIX, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_NIS_SERVERS, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_NISP_SERVERS, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_NIS_DOMAIN_NAME, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_NISP_DOMAIN_NAME, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_SNTP_SERVERS, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_INFORMATION_REFRESH_TIME, Dhcpv6Handler::Uint32bit},
    {DHCPV6_OPTION_BCMCS_SERVER_D, Dhcpv6Handler::NameCompressionArray},
    {DHCPV6_OPTION_BCMCS_SERVER_A, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_GEOCONF_CIVIC, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_REMOTE_ID, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_SUBSCRIBER_ID, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_CLIENT_FQDN, Dhcpv6Handler::ByteNameCompression},
    {DHCPV6_OPTION_PANA_AGENT, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_NEW_POSIX_TIMEZONE, Dhcpv6Handler::String},
    {DHCPV6_OPTION_NEW_TZDB_TIMEZONE, Dhcpv6Handler::String},
    {DHCPV6_OPTION_ERO, Dhcpv6Handler::Uint16bitArray},
    {DHCPV6_OPTION_LQ_QUERY, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_CLIENT_DATA, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_CLT_TIME, Dhcpv6Handler::Uint32bit},
    {DHCPV6_OPTION_LQ_RELAY_DATA, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_LQ_CLIENT_LINK, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_MIP6_HNIDF, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_MIP6_VDINF, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_V6_LOST, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_CAPWAP_AC_V6, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_RELAY_ID, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_IPv6_Address_MoS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_IPv6_FQDN_MoS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_NTP_SERVER, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_V6_ACCESS_DOMAIN, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_SIP_UA_CS_LIST, Dhcpv6Handler::NameCompressionArray},
    {DHCPV6_OPT_BOOTFILE_URL, Dhcpv6Handler::String},
    {DHCPV6_OPT_BOOTFILE_PARAM, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_CLIENT_ARCH_TYPE, Dhcpv6Handler::Uint16bitArray},
    {DHCPV6_OPTION_NII, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_GEOLOCATION, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_AFTR_NAME, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_ERP_LOCAL_DOMAIN_NAME, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_RSOO, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_PD_EXCLUDE, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_VSS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_MIP6_IDINF, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_MIP6_UDINF, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_MIP6_HNP, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_MIP6_HAA, Dhcpv6Handler::OneIPv6},
    {DHCPV6_OPTION_MIP6_HAF, Dhcpv6Handler::NameCompression},
    {DHCPV6_OPTION_RDNSS_SELECTION, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_KRB_PRINCIPAL_NAME, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_KRB_REALM_NAME, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_KRB_DEFAULT_REALM_NAME, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_KRB_KDC, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_CLIENT_LINKLAYER_ADDR, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_LINK_ADDRESS, Dhcpv6Handler::OneIPv6},
    {DHCPV6_OPTION_RADIUS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_SOL_MAX_RT, Dhcpv6Handler::Uint32bit},
    {DHCPV6_OPTION_INF_MAX_RT, Dhcpv6Handler::Uint32bit},
    {DHCPV6_OPTION_ADDRSEL, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_ADDRSEL_TABLE, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_V6_PCP_SERVER, Dhcpv6Handler::OneIPv6Plus},
    {DHCPV6_OPTION_DHCPV4_MSG, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_DHCP4_O_DHCP6_SERVER, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_RULE, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_BR, Dhcpv6Handler::OneIPv6},
    {DHCPV6_OPTION_S46_DMR, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_V4V6BIND, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_PORTPARAMS, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_CONT_MAPE, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_CONT_MAPT, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_S46_CONT_LW, Dhcpv6Handler::ByteArray},
    {DHCPV6_OPTION_IPv6_ADDRESS_ANDSF, Dhcpv6Handler::OneIPv6Plus}};

Dhcpv6Handler::Dhcpv6Handler(Agent *agent, boost::shared_ptr<PktInfo> info,
                             boost::asio::io_service &io)
        : DhcpHandlerBase(agent, info, io),
          msg_type_(DHCPV6_UNKNOWN), out_msg_type_(DHCPV6_UNKNOWN),
          rapid_commit_(false), reconfig_accept_(false),
          client_duid_len_(0), server_duid_len_(0),
          client_duid_(NULL), server_duid_(NULL), is_ia_na_(true) {
    memset(xid_, 0, sizeof(xid_));
    option_.reset(new Dhcpv6OptionHandler(NULL));
}

Dhcpv6Handler::~Dhcpv6Handler() {
}

bool Dhcpv6Handler::Run() {
    Dhcpv6Proto *dhcp_proto = agent()->dhcpv6_proto();
    // options length = pkt length - size of headers
    int16_t options_len = pkt_info_->len -
                          (pkt_info_->data - (uint8_t *)pkt_info_->pkt)
                          - DHCPV6_FIXED_LEN;
    if (options_len < 0) {
        dhcp_proto->IncrStatsError();
        DHCPV6_TRACE(Error, "Improper DHCPv6 packet length; vrf = " <<
                     pkt_info_->vrf << " ifindex = " << GetInterfaceIndex());
        return true;
    }

    dhcp_ = (Dhcpv6Hdr *) pkt_info_->data;
    option_->SetDhcpOptionPtr((uint8_t *)dhcp_ + DHCPV6_FIXED_LEN);
    // request_.UpdateData(dhcp_->xid, ntohs(dhcp_->flags), dhcp_->chaddr);
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
    if (!vm_itf_->dhcp_enable_config()) {
        dhcp_proto->IncrStatsError();
        DHCPV6_TRACE(Error, "DHCP request on VM port with dhcp services disabled: "
                     << GetInterfaceIndex());
        return true;
    }

    msg_type_ = dhcp_->type;
    memcpy(xid_, dhcp_->xid, 3);
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
    Dhcpv6Options *opt = reinterpret_cast<Dhcpv6Options *>(
        (uint8_t *)dhcp_ + DHCPV6_FIXED_LEN);
    // parse thru the option fields
    while (opt_rem_len > 0) {
        uint16_t option_code = ntohs(opt->code);
        uint16_t option_len = ntohs(opt->len);
        if (option_len > opt_rem_len) {
            DHCPV6_TRACE(Error, "DHCP option parsing error");
            break;
        }
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
                is_ia_na_ = true;
                ReadIA(opt->data, option_len, option_code);
                break;

            case DHCPV6_OPTION_IA_TA:
                is_ia_na_ = false;
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
    if (ia_na_.get() == NULL) {
        ia_na_.reset(new Dhcpv6IaData());
    }

    ia_na_->AddIa((Dhcpv6Ia *)ptr);

    int16_t iana_rem_len = len - sizeof(Dhcpv6Ia);
    Dhcpv6Options *iana_option = (Dhcpv6Options *)(ptr + sizeof(Dhcpv6Ia));
    while (iana_rem_len > 0) {
        uint16_t iana_option_code = ntohs(iana_option->code);
        uint16_t iana_option_len = ntohs(iana_option->len);
        switch (iana_option_code) {
            case DHCPV6_OPTION_IAADDR: {
                ia_na_->AddIaAddr((Dhcpv6IaAddr *)iana_option->data);
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

    return true;
}

void Dhcpv6Handler::FillDhcpInfo(Ip6Address &addr, int plen,
                                 Ip6Address &gw, Ip6Address &dns) {
    config_.ip_addr = addr;
    config_.plen = plen;
    config_.gw_addr = gw;
    config_.dns_addr = dns;
}

bool Dhcpv6Handler::FindLeaseData() {
    Ip6Address ip = vm_itf_->primary_ip6_addr();
    FindDomainName(ip);
    if (vm_itf_->IsActive()) {
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
                Ip6Address default_gw;
                if (ipam[i].default_gw.is_v6()) {
                    default_gw = ipam[i].default_gw.to_v6();
                }
                Ip6Address service_address;
                if (ipam[i].dns_server.is_unspecified()) {
                    service_address = default_gw;
                } else {
                    if (ipam[i].dns_server.is_v6()) {
                        service_address = ipam[i].dns_server.to_v6();
                    }
                }
                FillDhcpInfo(ip, ipam[i].plen, default_gw, service_address);
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

// Add an IP address to the option
uint16_t Dhcpv6Handler::AddIP(uint16_t opt_len, const std::string &input) {
    boost::system::error_code ec;
    Ip6Address ip = Ip6Address::from_string(input, ec);
    if (!ec.value()) {
        option_->AppendData(16, ip.to_bytes().data(), &opt_len);
    } else {
        DHCPV6_TRACE(Error, "Invalid DHCP option " << option_->GetCode() <<
                     " for VM " << config_.ip_addr.to_string() <<
                     "; has to be IP address");
    }
    return opt_len;
}

// Add domain name from IPAM to the option
uint16_t Dhcpv6Handler::AddDomainNameOption(uint16_t opt_len) {
    if (ipam_type_.ipam_dns_method == "virtual-dns-server") {
        if (is_dns_enabled() && config_.domain_name_.size()) {
            // encode the domain name in the dns encoding format
            boost::scoped_array<uint8_t> domain_name(new uint8_t[config_.domain_name_.size() * 2 + 2]);
            uint16_t len = 0;
            BindUtil::AddName(domain_name.get(), config_.domain_name_, 0, 0, len);
            option_->WriteData(DHCPV6_OPTION_DOMAIN_LIST, len,
                               domain_name.get(), &opt_len);
        }
    }
    return opt_len;
}

uint16_t Dhcpv6Handler::FillDhcpv6Hdr() {
    Dhcpv6Proto *dhcp_proto = agent()->dhcpv6_proto();

    dhcp_->type = out_msg_type_;
    memcpy(dhcp_->xid, xid_, 3);

    uint16_t opt_len = 0;

    // server duid
    option_->SetNextOptionPtr(opt_len);
    option_->WriteData(DHCPV6_OPTION_SERVERID, sizeof(Dhcpv6Proto::Duid),
                       (void *)dhcp_proto->server_duid(), &opt_len);

    // client duid
    if (client_duid_len_) {
        option_->SetNextOptionPtr(opt_len);
        option_->WriteData(DHCPV6_OPTION_CLIENTID, client_duid_len_,
                           (void *)client_duid_.get(), &opt_len);
    }

    // IA
    option_->SetNextOptionPtr(opt_len);
    WriteIaOption(opt_len);

    // Add dhcp options coming from Config
    opt_len = AddConfigDhcpOptions(opt_len, true);

    // GW doesnt come in DHCPV6, it should come via router advertisement

    if (!is_flag_set(DHCPV6_OPTION_DNS_SERVERS)) {
        uint16_t old_opt_len = opt_len;
        option_->SetNextOptionPtr(opt_len);
        option_->WriteData(DHCPV6_OPTION_DNS_SERVERS, 0, NULL, &opt_len);
        opt_len = AddDnsServers(opt_len);
        // if there was no DNS server, revert the option
        if (opt_len == old_opt_len + option_->GetFixedLen())
            opt_len = old_opt_len;
    }

    if (!is_flag_set(DHCPV6_OPTION_DOMAIN_LIST)) {
        option_->SetNextOptionPtr(opt_len);
        opt_len = AddDomainNameOption(opt_len);
    }

    return (DHCPV6_FIXED_LEN + opt_len);
}

void Dhcpv6Handler::IncrementByteInAddress(Ip6Address::bytes_type &bytes, uint8_t index) {
    if (index > 15) {
        return;
    }
    bytes[index]++;
    if (bytes[index] != 0) {
        return;
    } else {
        IncrementByteInAddress(bytes, index - 1);
    }
}

Ip6Address Dhcpv6Handler::GetNextV6Address(uint8_t addr[16]) {
    Ip6Address::bytes_type bytes;
    for (int i = 0; i < 16; i++) {
        bytes[i] = addr[i];
    }
    IncrementByteInAddress(bytes, 15);
    return Ip6Address(bytes);
}

void Dhcpv6Handler::WriteIaOption(uint16_t &optlen) {
    if (ia_na_.get() == NULL) {
        return;
    }

    uint32_t alloc_unit = 1;
    // for ia_ta, send only one address
    if (vm_itf_ && vm_itf_->vn() && is_ia_na_) {
        alloc_unit = vm_itf_->vn()->GetAllocUnitFromIpam(config_.ip_addr);
    }
    if (alloc_unit > 128) {
        DHCPV6_TRACE(Error, "Alloc-unit(" << alloc_unit << ") in Ipam is"
                     " higher than supported value(128), using max supported"
                     " value");
        alloc_unit = 128;
    }
    Dhcpv6IaAddr ia_addr(config_.ip_addr.to_v6().to_bytes().data(),
                         config_.preferred_time,
                         config_.valid_time);

    uint16_t ia_option = (is_ia_na_)? DHCPV6_OPTION_IA_NA : DHCPV6_OPTION_IA_TA;
    uint16_t ia_option_length = (is_ia_na_)? sizeof(Dhcpv6Ia) : 16;
    option_->WriteData(ia_option, ia_option_length, (void *)&ia_na_->ia, &optlen);

    for (uint32_t i = 0; i < alloc_unit; i++) {
        Dhcpv6Options *ia_addr_opt = GetNextOptionPtr(optlen);
        if (msg_type_ != DHCPV6_CONFIRM ||
            (msg_type_ == DHCPV6_CONFIRM && ia_na_->DelIaAddr(ia_addr))) {
            ia_addr_opt->WriteData(DHCPV6_OPTION_IAADDR, sizeof(Dhcpv6IaAddr),
                                   (void *)&ia_addr, &optlen);
            option_->AddLen(sizeof(Dhcpv6IaAddr) + 4);
        }
        Ip6Address next_ip = GetNextV6Address(ia_addr.address);
        memcpy(ia_addr.address, next_ip.to_bytes().data(), 16);
    }

    // in case of confirm, if there are any remaining addresses in ia_na_
    // add them with status as "NotOnLink".
    if (msg_type_ == DHCPV6_CONFIRM && !ia_na_->ia_addr.empty()) {
        for (std::vector<Dhcpv6IaAddr>::iterator it = ia_na_->ia_addr.begin();
             it != ia_na_->ia_addr.end(); ++it) {
            memcpy(ia_addr.address, it->address, 16);
            Dhcpv6Options *ia_addr_opt = GetNextOptionPtr(optlen);
            ia_addr_opt->WriteData(DHCPV6_OPTION_IAADDR, sizeof(Dhcpv6IaAddr),
                                   (void *)&ia_addr, &optlen);
            option_->AddLen(sizeof(Dhcpv6IaAddr) + 4);
            ia_addr_opt = GetNextOptionPtr(optlen);
            std::string message = "Some of the addresses are not on link";
            uint16_t status_code = htons(DHCPV6_NOT_ON_LINK);
            ia_addr_opt->WriteData(DHCPV6_OPTION_STATUS_CODE, 2,
                                   (void *)&status_code, &optlen);
            ia_addr_opt->AppendData(message.length(), message.c_str(), &optlen);
            option_->AddLen(message.length() + 6);
        }
    }
}

uint16_t Dhcpv6Handler::FillDhcpResponse(const MacAddress &dest_mac,
                                         Ip6Address src_ip, Ip6Address dest_ip) {
    pkt_info_->eth = (struct ether_header *)(pkt_info_->pkt);
    uint16_t eth_len = 0;
    eth_len = EthHdr((char *)pkt_info_->eth,
                     pkt_info_->packet_buffer()->data_len(),
                     GetInterfaceIndex(),
                     agent()->vhost_interface()->mac(), dest_mac,
                     ETHERTYPE_IPV6);
    pkt_info_->ip6 = (struct ip6_hdr *)((char *)pkt_info_->eth + eth_len);
    pkt_info_->transp.udp = (udphdr *)(pkt_info_->ip6 + 1);
    dhcp_ = (Dhcpv6Hdr *)(pkt_info_->transp.udp + 1);
    option_->SetDhcpOptionPtr((uint8_t *)dhcp_ + DHCPV6_FIXED_LEN);

    uint16_t len = FillDhcpv6Hdr();
    len += sizeof(udphdr);
    UdpHdr(len, src_ip.to_bytes().data(), DHCPV6_SERVER_PORT,
           dest_ip.to_bytes().data(), DHCPV6_CLIENT_PORT, IPPROTO_UDP);
    Ip6Hdr(pkt_info_->ip6, len, IPPROTO_UDP, 64, src_ip.to_bytes().data(),
           dest_ip.to_bytes().data());
    len += sizeof(ip6_hdr);

    pkt_info_->set_len(len + eth_len);
    return pkt_info_->packet_buffer()->data_len();
}

void Dhcpv6Handler::SendDhcpResponse() {
    UpdateStats();
    // In TSN, the source address for DHCP response should be the address
    // in the subnet reserved for service node. Otherwise, it will be the
    // GW address. dns_addr field has this address, use it as the source IP.
    FillDhcpResponse(MacAddress(pkt_info_->eth->ether_shost),
                     config_.dns_addr.to_v6(),
                     pkt_info_->ip_saddr.to_v6());
    uint32_t interface =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        pkt_info_->agent_hdr.cmd_param : GetInterfaceIndex();
    uint16_t command =
        (pkt_info_->agent_hdr.cmd == AgentHdr::TRAP_TOR_CONTROL_PKT) ?
        (uint16_t)AgentHdr::TX_ROUTE : AgentHdr::TX_SWITCH;
    Send(interface, pkt_info_->vrf, command, PktHandler::DHCPV6);
}

void Dhcpv6Handler::UpdateStats() {
    Dhcpv6Proto *dhcp_proto = agent()->dhcpv6_proto();
    (out_msg_type_ == DHCPV6_ADVERTISE) ? dhcp_proto->IncrStatsAdvertise() :
                                          dhcp_proto->IncrStatsReply();
}

Dhcpv6Handler::DhcpOptionCategory
Dhcpv6Handler::OptionCategory(uint32_t option) const {
    Dhcpv6CategoryIter iter = g_dhcpv6_category_map.find(option);
    if (iter == g_dhcpv6_category_map.end())
        return None;
    return iter->second;
}

uint32_t Dhcpv6Handler::OptionCode(const std::string &option) const {
    Dhcpv6NameCodeIter iter =
        g_dhcpv6_namecode_map.find(boost::to_lower_copy(option));
    return (iter == g_dhcpv6_namecode_map.end()) ? 0 : iter->second;
}

void Dhcpv6Handler::DhcpTrace(const std::string &msg) const {
    DHCPV6_TRACE(Error, "VM " << config_.ip_addr.to_string() << " : " << msg);
}
