/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_proto_hpp
#define vnsw_agent_dhcp_proto_hpp

#include "pkt/proto.h"
#include "services/dhcp_handler.h"

class Interface;

class DhcpProto : public Proto {
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
    };

    void Init();
    void Shutdown();
    DhcpProto(Agent *agent, boost::asio::io_service &io, bool run_with_vrouter);
    virtual ~DhcpProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);

    Interface *ip_fabric_interface() const { return ip_fabric_interface_; }
    void set_ip_fabric_interface(Interface *itf) { ip_fabric_interface_ = itf; }
    uint16_t ip_fabric_interface_index() const {
        return ip_fabric_interface_index_;
    }
    void set_ip_fabric_interface_index(uint16_t ind) {
        ip_fabric_interface_index_ = ind;
    }
    const unsigned char *ip_fabric_interface_mac() const {
        return ip_fabric_interface_mac_;
    }
    void set_ip_fabric_interface_mac(char *mac) {
        memcpy(ip_fabric_interface_mac_, mac, ETH_ALEN);
    }

    void IncrStatsDiscover() { stats_.discover++; }
    void IncrStatsRequest() { stats_.request++; }
    void IncrStatsInform() { stats_.inform++; }
    void IncrStatsDecline() { stats_.decline++; }
    void IncrStatsOther() { stats_.other++; }
    void IncrStatsOffers() { stats_.offers++; }
    void IncrStatsAcks() { stats_.acks++; }
    void IncrStatsNacks() { stats_.nacks++; }
    void IncrStatsRelayReqs() { stats_.relay_req++; }
    void IncrStatsRelayResps() { stats_.relay_resp++; }
    void IncrStatsErrors() { stats_.errors++; }
    DhcpStats GetStats() { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    void ItfNotify(DBEntryBase *entry);

    bool run_with_vrouter_;
    Interface *ip_fabric_interface_;
    uint16_t ip_fabric_interface_index_;
    unsigned char ip_fabric_interface_mac_[ETH_ALEN];
    DBTableBase::ListenerId iid_;
    DhcpStats stats_;

    DISALLOW_COPY_AND_ASSIGN(DhcpProto);
};

#endif // vnsw_agent_dhcp_proto_hpp
