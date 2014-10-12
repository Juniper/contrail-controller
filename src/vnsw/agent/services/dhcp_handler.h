/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_handler_hpp
#define vnsw_agent_dhcp_handler_hpp

#include "dhcp_handler_base.h"

#define DHCP_PKT_SIZE                 1024

// Magic cookie for DHCP Options 
#define DHCP_OPTIONS_COOKIE "\143\202\123\143"

// Supported DHCP options
#define DHCP_OPTION_PAD                        0
#define DHCP_OPTION_SUBNET_MASK                1
#define DHCP_OPTION_TIME_OFFSET                2   // deprecated
#define DHCP_OPTION_ROUTER                     3
#define DHCP_OPTION_TIME_SERVER                4
#define DHCP_OPTION_NAME_SERVER                5
#define DHCP_OPTION_DNS                        6
#define DHCP_OPTION_LOG_SERVER                 7
#define DHCP_OPTION_QUOTE_SERVER               8
#define DHCP_OPTION_LPR_SERVER                 9
#define DHCP_OPTION_IMPRESS_SERVER             10
#define DHCP_OPTION_RESOURCE_LOCATION_SERVER   11
#define DHCP_OPTION_HOST_NAME                  12
#define DHCP_OPTION_BOOT_FILE_SIZE             13
#define DHCP_OPTION_MERIT_DUMP_FILE            14
#define DHCP_OPTION_DOMAIN_NAME                15
#define DHCP_OPTION_SWAP_SERVER                16
#define DHCP_OPTION_ROOT_PATH                  17
#define DHCP_OPTION_EXTENSION_PATH             18
#define DHCP_OPTION_IP_FWD_CONTROL             19
#define DHCP_OPTION_NL_SRC_ROUTING             20
#define DHCP_OPTION_POLICY_FILTER              21
#define DHCP_OPTION_MAX_DG_REASSEMBLY_SIZE     22
#define DHCP_OPTION_DEFAULT_IP_TTL             23
#define DHCP_OPTION_PATH_MTU_AGING_TIMEOUT     24
#define DHCP_OPTION_PATH_MTU_PLATEAU_TABLE     25
#define DHCP_OPTION_INTERFACE_MTU              26
#define DHCP_OPTION_ALL_SUBNETS_LOCAL          27
#define DHCP_OPTION_BCAST_ADDRESS              28
#define DHCP_OPTION_PERFORM_MASK_DISCOVERY     29
#define DHCP_OPTION_MASK_SUPPLIER              30
#define DHCP_OPTION_PERFORM_ROUTER_DISCOVERY   31
#define DHCP_OPTION_ROUTER_SOLICIT_ADDRESS     32
#define DHCP_OPTION_STATIC_ROUTING_TABLE       33
#define DHCP_OPTION_TRAILER_ENCAP              34
#define DHCP_OPTION_ARP_CACHE_TIMEOUT          35
#define DHCP_OPTION_ETHERNET_ENCAP             36
#define DHCP_OPTION_DEFAULT_TCP_TTL            37
#define DHCP_OPTION_TCP_KEEPALIVE_INTERVAL     38
#define DHCP_OPTION_TCP_KEEPALIVE_GARBAGE      39
#define DHCP_OPTION_NIS_DOMAIN                 40
#define DHCP_OPTION_NIS_SERVERS                41
#define DHCP_OPTION_NTP_SERVERS                42
#define DHCP_OPTION_VENDOR_SPECIFIC_INFO       43
#define DHCP_OPTION_NETBIOS_OVER_TCP_NS        44
#define DHCP_OPTION_NETBIOS_OVER_TCP_DG_DS     45
#define DHCP_OPTION_NETBIOS_OVER_TCP_NODE_TYPE 46
#define DHCP_OPTION_NETBIOS_OVER_TCP_SCOPE     47
#define DHCP_OPTION_XWINDOW_FONT_SERVER        48
#define DHCP_OPTION_XWINDOW_SYSTEM_DISP_MGR    49
#define DHCP_OPTION_REQ_IP_ADDRESS             50
#define DHCP_OPTION_IP_LEASE_TIME              51
#define DHCP_OPTION_OVERLOAD                   52
#define DHCP_OPTION_MSG_TYPE                   53
#define DHCP_OPTION_SERVER_IDENTIFIER          54
#define DHCP_OPTION_PARAMETER_REQUEST_LIST     55
#define DHCP_OPTION_MESSAGE                    56
#define DHCP_OPTION_MAX_DHCP_MSG_SIZE          57
#define DHCP_OPTION_RENEW_TIME_VALUE           58
#define DHCP_OPTION_REBIND_TIME_VALUE          59
#define DHCP_OPTION_CLASS_ID                   60
#define DHCP_OPTION_CLIENT_ID                  61
#define DHCP_OPTION_NETWARE_IP_DOMAIN_NAME     62
#define DHCP_OPTION_NETWARE_IP_INFO            63
#define DHCP_OPTION_NIS_PLUS_DOMAIN            64
#define DHCP_OPTION_NIS_PLUS_SERVERS           65
#define DHCP_OPTION_TFTP_SERVER_NAME           66
#define DHCP_OPTION_BOOTFILE_NAME              67
#define DHCP_OPTION_MOBILE_IP_HA               68
#define DHCP_OPTION_SMTP_SERVER                69
#define DHCP_OPTION_POP_SERVER                 70
#define DHCP_OPTION_NNTP_SERVER                71
#define DHCP_OPTION_DEFAULT_WWW_SERVER         72
#define DHCP_OPTION_DEFAULT_FINGER_SERVER      73
#define DHCP_OPTION_DEFAULT_IRC_SERVER         74
#define DHCP_OPTION_STREETTALK_SERVER          75
#define DHCP_OPTION_STREETTALK_DA_SERVER       76
#define DHCP_OPTION_USER_CLASS_INFO            77
#define DHCP_OPTION_SLP_DIRECTORY_AGENT        78
#define DHCP_OPTION_SLP_SERVICE_SCOPE          79
#define DHCP_OPTION_RAPID_COMMIT               80
#define DHCP_OPTION_CLIENT_FQDN                81
#define DHCP_OPTION_82                         82
#define DHCP_OPTION_STORAGE_NS                 83
// ignoring option 84 (removed / unassigned)
#define DHCP_OPTION_NDS_SERVERS                85
#define DHCP_OPTION_NDS_TREE_NAME              86
#define DHCP_OPTION_NDS_CONTEXT                87
#define DHCP_OPTION_BCMCS_DN_LIST              88
#define DHCP_OPTION_BCMCS_ADDR_LIST            89
#define DHCP_OPTION_AUTH                       90
#define DHCP_OPTION_CLIENT_LAST_XTIME          91
#define DHCP_OPTION_ASSOCIATE_IP               92
#define DHCP_OPTION_CLIENT_SYSARCH_TYPE        93
#define DHCP_OPTION_CLIENT_NW_INTERFACE_ID     94
#define DHCP_OPTION_LDAP                       95
// ignoring 96 (removed / unassigned)
#define DHCP_OPTION_CLIENT_MACHINE_ID          97
#define DHCP_OPTION_OPENGROUP_USER_AUTH        98
#define DHCP_OPTION_GEOCONF_CIVIC              99
#define DHCP_OPTION_IEEE_1003_1_TZ             100
#define DHCP_OPTION_REF_TZ_DB                  101
// ignoring 102 to 111 & 115 (removed / unassigned)
#define DHCP_OPTION_NETINFO_PARENT_SERVER_ADDR 112
#define DHCP_OPTION_NETINFO_PARENT_SERVER_TAG  113
#define DHCP_OPTION_URL                        114
#define DHCP_OPTION_AUTO_CONFIGURE             116
#define DHCP_OPTION_NAME_SERVICE_SEARCH        117
#define DHCP_OPTION_SUBNET_SELECTION           118
#define DHCP_OPTION_DNS_DOMAIN_SEARCH_LIST     119
#define DHCP_OPTION_SIP_SERVERS                120
#define DHCP_OPTION_CLASSLESS_ROUTE            121
#define DHCP_OPTION_CCC                        122
#define DHCP_OPTION_GEOCONF                    123
#define DHCP_OPTION_VENDOR_ID_VENDOR_CLASS     124
#define DHCP_OPTION_VENDOR_ID_VENDOR_SPECIFIC  125
// ignoring 126, 127 (removed / unassigned)
// options 128 - 135 arent officially assigned to PXE
#define DHCP_OPTION_TFTP_SERVER                128
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_129    129
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_130    130
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_131    131
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_132    132
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_133    133
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_134    134
#define DHCP_OPTION_PXE_VENDOR_SPECIFIC_135    135
#define DHCP_OPTION_PANA_AUTH_AGENT            136
#define DHCP_OPTION_LOST_SERVER                137
#define DHCP_OPTION_CAPWAP_AC_ADDRESS          138
#define DHCP_OPTION_IPV4_ADDRESS_MOS           139
#define DHCP_OPTION_IPV4_FQDN_MOS              140
#define DHCP_OPTION_SIP_UA_CONFIG_DOMAIN       141
#define DHCP_OPTION_IPV4_ADDRESS_ANDSF         142
#define DHCP_OPTION_GEOLOC                     144
#define DHCP_OPTION_FORCERENEW_NONCE_CAP       145
#define DHCP_OPTION_RDNSS_SELECTION            146
// ignoring options 143, 147 - 149 (removed / unassigned)
// option 150 is also assigned as Etherboot, GRUB configuration path name
#define DHCP_OPTION_TFTP_SERVER_ADDRESS        150
#define DHCP_OPTION_STATUS_CODE                151
#define DHCP_OPTION_BASE_TIME                  152
#define DHCP_OPTION_START_TIME_OF_STATE        153
#define DHCP_OPTION_QUERY_START_TIME           154
#define DHCP_OPTION_QUERY_END_TIME             155
#define DHCP_OPTION_DHCP_STATE                 156
#define DHCP_OPTION_DATA_SOURCE                157
#define DHCP_OPTION_PCP_SERVER                 158
// ignoring options 159 - 174 (removed / unassigned)
// ignoring options 175 - 177 (tentatively assigned)
// ignoring options 178 - 207 (removed / unassigned)
#define DHCP_OPTION_PXELINUX_MAGIC             208  // deprecated
#define DHCP_OPTION_CONFIG_FILE                209
#define DHCP_OPTION_PATH_PREFIX                210
#define DHCP_OPTION_REBOOT_TIME                211
#define DHCP_OPTION_6RD                        212
#define DHCP_OPTION_V4_ACCESS_DOMAIN           213
// ignoring options 214 - 219 (removed / unassigned)
#define DHCP_OPTION_SUBNET_ALLOCATION          220
#define DHCP_OPTION_VSS                        221
// ignoring options 222 - 254 (removed / unassigned)
#define DHCP_OPTION_END                        255

#define DHCP_SUBOP_CKTID              1
#define DHCP_SUBOP_REMOTEID           2

// DHCP message types
#define DHCP_UNKNOWN          0
#define DHCP_DISCOVER         1
#define DHCP_OFFER            2
#define DHCP_REQUEST          3
#define DHCP_DECLINE          4
#define DHCP_ACK              5
#define DHCP_NAK              6
#define DHCP_RELEASE          7
#define DHCP_INFORM           8
#define DHCP_LEASE_QUERY      10
#define DHCP_LEASE_UNASSIGNED 11
#define DHCP_LEASE_UNKNOWN    12
#define DHCP_LEASE_ACTIVE     13

#define DHCP_CHADDR_LEN     16
#define DHCP_NAME_LEN       64
#define DHCP_FILE_LEN       128
#define DHCP_FIXED_LEN      236
#define DHCP_MAX_OPTION_LEN 1236

#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68

#define BOOT_REQUEST        1
#define BOOT_REPLY          2
#define DHCP_BCAST_FLAG     0x8000
#define HW_TYPE_ETHERNET    1

#define DHCP_SHORTLEASE_TIME 4
#define LINK_LOCAL_SUBNET    "169.254.0.0"

struct dhcphdr {
     uint8_t  op;
     uint8_t  htype;
     uint8_t  hlen;
     uint8_t  hops;  // # of relay agent hops 
     uint32_t xid;
     uint16_t secs;
     uint16_t flags;
     in_addr_t ciaddr;
     in_addr_t yiaddr;
     in_addr_t siaddr;
     in_addr_t giaddr;
     uint8_t chaddr[DHCP_CHADDR_LEN];
     uint8_t sname[DHCP_NAME_LEN];
     uint8_t file[DHCP_FILE_LEN];
     uint8_t options[DHCP_MAX_OPTION_LEN];
};

struct Dhcpv4Options {
    void WriteData(uint8_t c, uint8_t l, const void *d, uint16_t *optlen) {
        code = c;
        len = l; 
        memcpy(data, (uint8_t *)d, l);
        *optlen += 2 + l;
    }
    void WriteByte(uint8_t c, uint16_t *optlen) {
        code = c;
        *optlen += 1;
    }
    void AppendData(uint16_t l, const void *d, uint16_t *optlen) {
        memcpy(data + len, (uint8_t *)d, l);
        len += l;
        *optlen += l;
    }
    Dhcpv4Options *GetNextOptionPtr() {
        uint8_t *next = reinterpret_cast<uint8_t *>(this);
        if (code == DHCP_OPTION_PAD || code == DHCP_OPTION_END)
            return reinterpret_cast<Dhcpv4Options *>(next + 1);
        else
            return reinterpret_cast<Dhcpv4Options *>(next + len + 2);
    }

    uint8_t code;
    uint8_t len;
    uint8_t data[0];
};

// DHCP protocol handler
class DhcpHandler : public DhcpHandlerBase {
public:

    struct DhcpRequestData {
        DhcpRequestData() : xid(-1), flags(0), ip_addr(0) {
            memset(mac_addr, 0, ETHER_ADDR_LEN);
        }
        void UpdateData(uint32_t id, uint16_t fl, uint8_t *mac) {
            xid = id;
            flags = fl;
            memcpy(mac_addr, mac, ETHER_ADDR_LEN);
        }

        uint32_t  xid;
        uint16_t  flags;
        uint8_t   mac_addr[ETHER_ADDR_LEN];
        in_addr_t ip_addr;
    };

    struct Dhcpv4OptionHandler : DhcpOptionHandler {
        static const uint16_t kDhcpOptionFixedLen = 2;

        explicit Dhcpv4OptionHandler(uint8_t *ptr) { dhcp_option_ptr = ptr; }
        void WriteData(uint8_t c, uint8_t l, const void *d, uint16_t *optlen) {
            option->WriteData(c, l, d, optlen);
        }
        void WriteByte(uint8_t c, uint16_t *optlen) {
            option->WriteByte(c, optlen);
        }
        void AppendData(uint16_t l, const void *d, uint16_t *optlen) {
            option->AppendData(l, d, optlen);
        }
        uint16_t GetCode() const { return option->code; }
        uint16_t GetLen() const { return option->len; }
        uint16_t GetFixedLen() const { return kDhcpOptionFixedLen; }
        uint8_t *GetData() { return option->data; }
        void SetCode(uint16_t c) { option->code = (uint8_t) c; }
        void SetLen(uint16_t l) { option->len = (uint8_t) l; }
        void AddLen(uint16_t l) { option->len += (uint8_t) l; }
        void SetNextOptionPtr(uint16_t optlen) {
            option = (Dhcpv4Options *)(dhcp_option_ptr + optlen);
        }
        void SetDhcpOptionPtr(uint8_t *ptr) { dhcp_option_ptr = ptr; }

        uint8_t *dhcp_option_ptr; // pointer to DHCP options in the packet
        Dhcpv4Options *option;    // pointer to current option being processed
    };

    DhcpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                boost::asio::io_service &io);
    virtual ~DhcpHandler();

    bool Run();

private:
    bool HandleVmRequest();
    bool HandleMessage();
    bool HandleDhcpFromFabric();
    bool ReadOptions(int16_t opt_rem_len);
    bool FindLeaseData();
    void FillDhcpInfo(Ip4Address &addr, int plen,
                      Ip4Address &gw, Ip4Address &dns);
    void WriteOption82(Dhcpv4Options *opt, uint16_t *optlen);
    bool ReadOption82(Dhcpv4Options *opt);
    bool CreateRelayPacket();
    bool CreateRelayResponsePacket();
    void RelayRequestToFabric();
    void RelayResponseFromFabric();
    uint16_t DhcpHdr(in_addr_t, in_addr_t);
    uint16_t AddIP(uint16_t opt_len, const std::string &input);
    uint16_t AddDomainNameOption(uint16_t opt_len);
    uint16_t FillDhcpResponse(const MacAddress &dest_mac,
                              in_addr_t src_ip, in_addr_t dest_ip,
                              in_addr_t siaddr, in_addr_t yiaddr);
    void SendDhcpResponse();
    bool IsOptionRequested(uint8_t option);
    bool IsRouterOptionNeeded();
    void UpdateStats();
    DhcpHandler::DhcpOptionCategory OptionCategory(uint32_t option) const;
    uint32_t OptionCode(const std::string &option) const;
    void DhcpTrace(const std::string &msg) const;

    dhcphdr *dhcp_;
    uint8_t msg_type_;
    uint8_t out_msg_type_;

    std::string parameters_; // options requested by the DHCP client
    std::string nak_msg_;
    DhcpRequestData request_;
    DISALLOW_COPY_AND_ASSIGN(DhcpHandler);
};

typedef std::map<std::string, uint32_t> Dhcpv4NameCodeMap;
typedef std::map<std::string, uint32_t>::const_iterator Dhcpv4NameCodeIter;
typedef std::map<uint32_t, DhcpHandler::DhcpOptionCategory> Dhcpv4CategoryMap;
typedef std::map<uint32_t, DhcpHandler::DhcpOptionCategory>::const_iterator Dhcpv4CategoryIter;

#endif // vnsw_agent_dhcp_handler_hpp
