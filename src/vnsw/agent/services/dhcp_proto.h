/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_proto_hpp
#define vnsw_agent_dhcp_proto_hpp

#include <set>
#include "pkt/proto.h"
#include "vnc_cfg_types.h"

#define DHCP_PKT_SIZE                 1024

// Magic cookie for DHCP Options 
#define DHCP_OPTIONS_COOKIE "\143\202\123\143"

// Supported DHCP options
#define DHCP_OPTION_PAD               0
#define DHCP_OPTION_SUBNET_MASK       1
#define DHCP_OPTION_ROUTER            3
#define DHCP_OPTION_NTP               4
#define DHCP_OPTION_DNS               6
#define DHCP_OPTION_HOST_NAME         12
#define DHCP_OPTION_DOMAIN_NAME       15
#define DHCP_OPTION_BCAST_ADDRESS     28
#define DHCP_OPTION_REQ_IP_ADDRESS    50
#define DHCP_OPTION_IP_LEASE_TIME     51
#define DHCP_OPTION_OVERLOAD          52
#define DHCP_OPTION_MSG_TYPE          53
#define DHCP_OPTION_SERVER_IDENTIFIER 54
#define DHCP_OPTION_MESSAGE           56
#define DHCP_OPTION_CLIENT_FQDN       81
#define DHCP_OPTION_82                82
#define DHCP_OPTION_END               255

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

class Interface;

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

struct ConfigRecord {
    uint32_t ip_addr;
    uint32_t subnet_mask;
    uint32_t bcast_addr;
    uint32_t gw_addr;
    uint32_t dns_addr;
    uint8_t  mac_addr[MAC_ALEN];
    uint16_t ifindex;  // maps to VNid, VMid, itf
    uint32_t plen;
    uint32_t lease_time;

    ConfigRecord() : ip_addr(0), subnet_mask(0), bcast_addr(0), gw_addr(0), 
                     dns_addr(0), ifindex(0), plen(0), lease_time(-1) {
        memset(mac_addr, 0, MAC_ALEN);
    }
};

struct DhcpOptions {
    uint8_t code;
    uint8_t len;
    char data[0];

    void WriteData(uint8_t c, uint8_t l, const void *d, uint16_t &optlen) {
        code = c;
        len = l; 
        memcpy(data, (uint8_t *)d, l);
        optlen += 2 + l;
    }
    void WriteWord(uint8_t c, uint32_t value, uint16_t &optlen) {
        uint32_t net = htonl(value);
        WriteData(c, 4, &net, optlen);
    }
    void WriteByte(uint8_t c, uint16_t &optlen) {
        code = c;
        optlen += 1;
    }
    DhcpOptions *GetNextOptionPtr() {
        uint8_t *next = reinterpret_cast<uint8_t *>(this);
        if (code == DHCP_OPTION_PAD || code == DHCP_OPTION_END)
            return reinterpret_cast<DhcpOptions *>(next + 1);
        else
            return reinterpret_cast<DhcpOptions *>(next + len + 2);
    }
};

// DHCP protocol handler
class DhcpHandler : public ProtoHandler {
public:
    struct DhcpStats {
        uint32_t discover;
        uint32_t request;
        uint32_t inform;
        uint32_t decline;
        uint32_t other;
        uint32_t offers;
        uint32_t acks;
        uint32_t nacks;
        uint32_t relay_req;
        uint32_t relay_resp;
        uint32_t errors;

        void Reset() {
            discover = request = inform = decline = other = 
            offers = acks = nacks = errors = relay_req = relay_resp = 0;
        }
        DhcpStats() { Reset(); }
        void IncrStatsDiscover() { discover++; }
        void IncrStatsRequest() { request++; }
        void IncrStatsInform() { inform++; }
        void IncrStatsDecline() { decline++; }
        void IncrStatsOther() { other++; }
        void IncrStatsOffers() { offers++; }
        void IncrStatsAcks() { acks++; }
        void IncrStatsNacks() { nacks++; }
        void IncrStatsRelayReqs() { relay_req++; }
        void IncrStatsRelayResps() { relay_resp++; }
        void IncrStatsErrors() { errors++; }
    };

    DhcpHandler(PktInfo *info, boost::asio::io_service &io);
    virtual ~DhcpHandler() {};

    bool Run();

    static DhcpStats GetStats() { return stats_; }
    static void ClearStats() { stats_.Reset(); }

private:
    bool ReadOptions();
    bool FindLeaseData();
    void GetLinkLocalAddr(uint32_t id);
    void FillDhcpInfo(uint32_t addr, int plen, uint32_t gw, uint32_t dns);
    void UpdateDnsServer();
    void WriteOption82(DhcpOptions *opt, uint16_t &optlen);
    bool ReadOption82(DhcpOptions *opt);
    bool CreateRelayPacket(bool is_request);
    void RelayRequestToFabric();
    void RelayResponseFromFabric();
    uint16_t DhcpHdr(in_addr_t, in_addr_t, uint8_t *);
    void SendDhcpResponse();
    void UpdateStats() {
        (out_msg_type_ == DHCP_OFFER) ?  stats_.IncrStatsOffers() :
            ((out_msg_type_ == DHCP_ACK) ?  stats_.IncrStatsAcks() : 
            stats_.IncrStatsNacks());
    }
    std::string &MsgType(uint32_t msg_type);
    DhcpOptions *GetNextOptionPtr(uint16_t optlen) {
        return reinterpret_cast<DhcpOptions *>(dhcp_->options + optlen);
    }

    dhcphdr *dhcp_;
    VmPortInterface *vm_itf_;
    uint32_t vm_itf_index_;
    uint8_t msg_type_;
    uint8_t out_msg_type_;
    std::string client_name_;
    std::string domain_name_;
    in_addr_t req_ip_addr_;
    std::string nak_msg_;
    ConfigRecord config_;
    autogen::IpamType ipam_type_;
    autogen::VirtualDnsType vdns_type_;
    static DhcpStats stats_;
    DISALLOW_COPY_AND_ASSIGN(DhcpHandler);
};

class DhcpProto : public Proto<DhcpHandler> {
public:
    typedef std::set<Interface *> InterfaceList;

    static void Init(boost::asio::io_service &io, bool run_with_vrouter);
    static void Shutdown();
    virtual ~DhcpProto();

    static Interface *IPFabricIntf() { return ip_fabric_intf_; }
    static void IPFabricIntf(Interface *itf) { ip_fabric_intf_ = itf; }
    static uint16_t IPFabricIntfIndex() { return ip_fabric_intf_index_; }
    static void IPFabricIntfIndex(uint16_t ind) { ip_fabric_intf_index_ = ind; }
    static unsigned char *IPFabricIntfMac() { return ip_fabric_intf_mac_; }
    static void IPFabricIntfMac(char *mac) {
        memcpy(ip_fabric_intf_mac_, mac, MAC_ALEN);
    }

private:
    DhcpProto(boost::asio::io_service &io, bool run_with_vrouter);
    void ItfUpdate(DBEntryBase *entry);

    bool run_with_vrouter_;
    static Interface *ip_fabric_intf_;
    static uint16_t ip_fabric_intf_index_;
    static unsigned char ip_fabric_intf_mac_[MAC_ALEN];
    static DBTableBase::ListenerId iid_;

    DISALLOW_COPY_AND_ASSIGN(DhcpProto);
};

#endif // vnsw_agent_dhcp_proto_hpp
