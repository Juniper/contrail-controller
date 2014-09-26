/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcpv6_handler_hpp
#define vnsw_agent_dhcpv6_handler_hpp

#include "dhcp_handler_base.h"

#define ALL_DHCPV6_SERVERS_ADDRESS             "FF05::1:3"
#define ALL_DHCPV6_RELAYAGENTS_SERVERS_ADDRESS "FF05::1:3"

// Supported DHCPv6 options
#define DHCPV6_OPTION_UNKNOWN                  0
#define DHCPV6_OPTION_CLIENTID                 1
#define DHCPV6_OPTION_SERVERID                 2
#define DHCPV6_OPTION_IA_NA                    3
#define DHCPV6_OPTION_IA_TA                    4
#define DHCPV6_OPTION_IAADDR                   5
#define DHCPV6_OPTION_ORO                      6
#define DHCPV6_OPTION_PREFERENCE               7
#define DHCPV6_OPTION_ELAPSED_TIME             8
#define DHCPV6_OPTION_RELAY_MSG                9
// Option 10 Unassigned
#define DHCPV6_OPTION_AUTH                     11
#define DHCPV6_OPTION_UNICAST                  12
#define DHCPV6_OPTION_STATUS_CODE              13
#define DHCPV6_OPTION_RAPID_COMMIT             14
#define DHCPV6_OPTION_USER_CLASS               15
#define DHCPV6_OPTION_VENDOR_CLASS             16
#define DHCPV6_OPTION_VENDOR_OPTS              17
#define DHCPV6_OPTION_INTERFACE_ID             18
#define DHCPV6_OPTION_RECONF_MSG               19
#define DHCPV6_OPTION_RECONF_ACCEPT            20
#define DHCPV6_OPTION_SIP_SERVER_D             21
#define DHCPV6_OPTION_SIP_SERVER_A             22
#define DHCPV6_OPTION_DNS_SERVERS              23
#define DHCPV6_OPTION_DOMAIN_LIST              24
#define DHCPV6_OPTION_IA_PD                    25
#define DHCPV6_OPTION_IAPREFIX                 26
#define DHCPV6_OPTION_NIS_SERVERS              27
#define DHCPV6_OPTION_NISP_SERVERS             28
#define DHCPV6_OPTION_NIS_DOMAIN_NAME          29
#define DHCPV6_OPTION_NISP_DOMAIN_NAME         30
#define DHCPV6_OPTION_SNTP_SERVERS             31
#define DHCPV6_OPTION_INFORMATION_REFRESH_TIME 32
#define DHCPV6_OPTION_BCMCS_SERVER_D           33
#define DHCPV6_OPTION_BCMCS_SERVER_A           34
// Option 35 Unassigned
#define DHCPV6_OPTION_GEOCONF_CIVIC            36
#define DHCPV6_OPTION_REMOTE_ID                37
#define DHCPV6_OPTION_SUBSCRIBER_ID            38
#define DHCPV6_OPTION_CLIENT_FQDN              39
#define DHCPV6_OPTION_PANA_AGENT               40
#define DHCPV6_OPTION_NEW_POSIX_TIMEZONE       41
#define DHCPV6_OPTION_NEW_TZDB_TIMEZONE        42
#define DHCPV6_OPTION_ERO                      43
#define DHCPV6_OPTION_LQ_QUERY                 44
#define DHCPV6_OPTION_CLIENT_DATA              45
#define DHCPV6_OPTION_CLT_TIME                 46
#define DHCPV6_OPTION_LQ_RELAY_DATA            47
#define DHCPV6_OPTION_LQ_CLIENT_LINK           48
#define DHCPV6_OPTION_MIP6_HNIDF               49
#define DHCPV6_OPTION_MIP6_VDINF               50
#define DHCPV6_OPTION_V6_LOST                  51
#define DHCPV6_OPTION_CAPWAP_AC_V6             52
#define DHCPV6_OPTION_RELAY_ID                 53
#define DHCPV6_OPTION_IPv6_Address_MoS         54
#define DHCPV6_OPTION_IPv6_FQDN_MoS            55
#define DHCPV6_OPTION_NTP_SERVER               56
#define DHCPV6_OPTION_V6_ACCESS_DOMAIN         57
#define DHCPV6_OPTION_SIP_UA_CS_LIST           58
#define DHCPV6_OPT_BOOTFILE_URL                59
#define DHCPV6_OPT_BOOTFILE_PARAM              60
#define DHCPV6_OPTION_CLIENT_ARCH_TYPE         61
#define DHCPV6_OPTION_NII                      62
#define DHCPV6_OPTION_GEOLOCATION              63
#define DHCPV6_OPTION_AFTR_NAME                64
#define DHCPV6_OPTION_ERP_LOCAL_DOMAIN_NAME    65
#define DHCPV6_OPTION_RSOO                     66
#define DHCPV6_OPTION_PD_EXCLUDE               67
#define DHCPV6_OPTION_VSS                      68
#define DHCPV6_OPTION_MIP6_IDINF               69
#define DHCPV6_OPTION_MIP6_UDINF               70
#define DHCPV6_OPTION_MIP6_HNP                 71
#define DHCPV6_OPTION_MIP6_HAA                 72
#define DHCPV6_OPTION_MIP6_HAF                 73
#define DHCPV6_OPTION_RDNSS_SELECTION          74
#define DHCPV6_OPTION_KRB_PRINCIPAL_NAME       75
#define DHCPV6_OPTION_KRB_REALM_NAME           76
#define DHCPV6_OPTION_KRB_DEFAULT_REALM_NAME   77
#define DHCPV6_OPTION_KRB_KDC                  78
#define DHCPV6_OPTION_CLIENT_LINKLAYER_ADDR    79
#define DHCPV6_OPTION_LINK_ADDRESS             80
#define DHCPV6_OPTION_RADIUS                   81
#define DHCPV6_OPTION_SOL_MAX_RT               82
#define DHCPV6_OPTION_INF_MAX_RT               83
#define DHCPV6_OPTION_ADDRSEL                  84
#define DHCPV6_OPTION_ADDRSEL_TABLE            85
#define DHCPV6_OPTION_V6_PCP_SERVER            86
#define DHCPV6_OPTION_DHCPV4_MSG               87
#define DHCPV6_OPTION_DHCP4_O_DHCP6_SERVER     88
#define DHCPV6_OPTION_S46_RULE                 89
#define DHCPV6_OPTION_S46_BR                   90
#define DHCPV6_OPTION_S46_DMR                  91
#define DHCPV6_OPTION_S46_V4V6BIND             92
#define DHCPV6_OPTION_S46_PORTPARAMS           93
#define DHCPV6_OPTION_S46_CONT_MAPE            94
#define DHCPV6_OPTION_S46_CONT_MAPT            95
#define DHCPV6_OPTION_S46_CONT_LW              96
// options 97-142 Unassigned
#define DHCPV6_OPTION_IPv6_ADDRESS_ANDSF       143

// DHCPv6 message types
#define DHCPV6_UNKNOWN              0
#define DHCPV6_SOLICIT              1
#define DHCPV6_ADVERTISE            2
#define DHCPV6_REQUEST              3
#define DHCPV6_CONFIRM              4
#define DHCPV6_RENEW                5
#define DHCPV6_REBIND               6
#define DHCPV6_REPLY                7
#define DHCPV6_RELEASE              8
#define DHCPV6_DECLINE              9
#define DHCPV6_RECONFIGURE          10
#define DHCPV6_INFORMATION_REQUEST  11

// DHCPv6 status codes
#define DHCPV6_SUCCESS              0
#define DHCPV6_UNSPEC_FAIL          1
#define DHCPV6_NO_ADDRS_AVAIL       2
#define DHCPV6_NO_BINDING           3
#define DHCPV6_NOT_ON_LINK          4
#define DHCPV6_USE_MULTICAST        5

#define DHCPV6_SHORTLEASE_TIME      4
#define DHCP_PKT_SIZE               1024
#define MAX_DOMAIN_NAME_LENGTH      256

// fixed length = sizeof(type) + sizeof(xid)
#define DHCPV6_FIXED_LEN            4

struct Dhcpv6Options {
    void WriteData(uint16_t c, uint16_t l, const void *d, uint16_t *optlen) {
        code = htons(c);
        len = htons(l); 
        memcpy(data, (uint8_t *)d, l);
        *optlen += 4 + l;
    }
    void AppendData(uint16_t l, const void *d, uint16_t *optlen) {
        uint16_t curr_len = ntohs(len);
        memcpy(data + curr_len, (uint8_t *)d, l);
        len = htons(curr_len + l);
        *optlen += l;
    }
    void AddLen(uint16_t l) {
        len = htons(ntohs(len) + l);
    }
    uint16_t GetLen() const { return ntohs(len); }
    Dhcpv6Options *GetNextOptionPtr() {
        uint8_t *next = reinterpret_cast<uint8_t *>(this);
        return reinterpret_cast<Dhcpv6Options *>(next + ntohs(len) + 4);
    }

    uint16_t code;
    uint16_t len;
    uint8_t  data[0];
};

struct Dhcpv6Hdr {
    uint8_t     type;
    uint8_t     xid[3];
    Dhcpv6Options options[0];
};

struct Dhcpv6Ia {
    Dhcpv6Ia(Dhcpv6Ia *ptr) : iaid(ptr->iaid), t1(ptr->t1), t2(ptr->t2) {}

    uint32_t iaid;
    uint32_t t1;
    uint32_t t2;
    // followed by options
};

struct Dhcpv6IaAddr {
    Dhcpv6IaAddr(Dhcpv6IaAddr *ptr) {
        if (ptr) {
            memcpy(address, ptr->address, 16);
            preferred_lifetime = ptr->preferred_lifetime;
            valid_lifetime = ptr->valid_lifetime;
        } else {
            memset(address, 0, 16);
            preferred_lifetime = 0;
            valid_lifetime = 0;
        }
    }
    Dhcpv6IaAddr(uint8_t *addr, uint32_t pl, uint32_t vl) {
        memcpy(address, addr, 16);
        preferred_lifetime = htonl(pl);
        valid_lifetime = htonl(vl);
    }

    uint8_t address[16];
    uint32_t preferred_lifetime;
    uint32_t valid_lifetime;
    // followed by options
};

// DHCP protocol handler
class Dhcpv6Handler : public DhcpHandlerBase {
public:
    typedef boost::scoped_array<uint8_t> Duid;

    struct Dhcpv6IaData {
        Dhcpv6IaData(Dhcpv6Ia *ia_ptr, Dhcpv6IaAddr *ia_addr_ptr) :
            ia(ia_ptr), ia_addr(ia_addr_ptr) {}

        Dhcpv6Ia ia;
        Dhcpv6IaAddr ia_addr;
    };

    struct Dhcpv6OptionHandler : DhcpOptionHandler {
        static const uint16_t kDhcpOptionFixedLen = 4;

        explicit Dhcpv6OptionHandler(uint8_t *ptr) { dhcp_option_ptr = ptr; }
        void WriteData(uint8_t c, uint8_t l, const void *d, uint16_t *optlen) {
            option->WriteData(c, l, d, optlen);
        }
        void AppendData(uint16_t l, const void *d, uint16_t *optlen) {
            option->AppendData(l, d, optlen);
        }
        uint16_t GetCode() const { return ntohs(option->code); }
        uint16_t GetLen() const { return ntohs(option->len); }
        uint16_t GetFixedLen() const { return kDhcpOptionFixedLen; }
        uint8_t *GetData() { return option->data; }
        void SetCode(uint16_t c) { option->code = htons(c); }
        void SetLen(uint16_t l) { option->len = htons(l); }
        void AddLen(uint16_t l) { option->AddLen(l); }
        void SetNextOptionPtr(uint16_t optlen) {
            option = (Dhcpv6Options *)(dhcp_option_ptr + optlen);
        }
        void SetDhcpOptionPtr(uint8_t *ptr) { dhcp_option_ptr = ptr; }

        uint8_t *dhcp_option_ptr; // pointer to DHCP options in the packet
        Dhcpv6Options *option;    // pointer to current option being processed
    };

    Dhcpv6Handler(Agent *agent, boost::shared_ptr<PktInfo> info,
                  boost::asio::io_service &io);
    virtual ~Dhcpv6Handler();

    bool Run();

private:
    void ReadOptions(int16_t opt_rem_len);
    bool ReadIA(uint8_t *ptr, uint16_t len, uint16_t code);
    void FillDhcpInfo(Ip6Address &addr, int plen,
                      Ip6Address &gw, Ip6Address &dns);
    bool FindLeaseData();
    uint16_t AddIP(uint16_t opt_len, const std::string &input);
    uint16_t AddDomainNameOption(uint16_t opt_len);
    uint16_t FillDhcpv6Hdr();
    void WriteIaOption(const Dhcpv6Ia &ia, uint16_t &optlen);
    uint16_t FillDhcpResponse(unsigned char *dest_mac,
                              Ip6Address src_ip, Ip6Address dest_ip);
    void SendDhcpResponse();
    void UpdateStats();
    DhcpOptionCategory OptionCategory(uint32_t option) const;
    uint32_t OptionCode(const std::string &option) const;
    void DhcpTrace(const std::string &msg) const;
    Dhcpv6Options *GetNextOptionPtr(uint16_t optlen) {
        return reinterpret_cast<Dhcpv6Options *>((uint8_t *)dhcp_->options + optlen);
    }

    Dhcpv6Hdr *dhcp_;

    uint8_t msg_type_;
    uint8_t out_msg_type_;
    uint8_t xid_[3];

    // data from the incoming dhcp message
    bool rapid_commit_;
    bool reconfig_accept_;
    uint16_t client_duid_len_;
    uint16_t server_duid_len_;
    Duid client_duid_;    // client duid
    Duid server_duid_;    // server duid received in the request
    std::vector<Dhcpv6IaData> iana_;
    std::vector<Dhcpv6IaData> iata_;

    DISALLOW_COPY_AND_ASSIGN(Dhcpv6Handler);
};

typedef std::map<std::string, uint32_t> Dhcpv6NameCodeMap;
typedef std::map<std::string, uint32_t>::const_iterator Dhcpv6NameCodeIter;
typedef std::map<uint32_t, Dhcpv6Handler::DhcpOptionCategory> Dhcpv6CategoryMap;
typedef std::map<uint32_t, Dhcpv6Handler::DhcpOptionCategory>::const_iterator Dhcpv6CategoryIter;

#endif // vnsw_agent_dhcpv6_handler_hpp
