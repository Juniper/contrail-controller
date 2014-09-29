/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_proto_hpp
#define vnsw_agent_dhcp_proto_hpp

#include "pkt/proto.h"
#include "services/dhcp_handler.h"

#define DHCP_TRACE(obj, arg)                                                 \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Dhcp##obj::TraceMsg(DhcpTraceBuf, __FILE__, __LINE__, _str.str());       \
} while (false)                                                              \

class Interface;
typedef boost::asio::ip::udp boost_udp;

class DhcpProto : public Proto {
public:
    static const uint32_t kDhcpMaxPacketSize = 1024;

    enum DhcpMsgType {
        DHCP_VHOST_MSG,
    };

    struct DhcpVhostMsg : InterTaskMsg {
        uint8_t *pkt;
        uint32_t len;

        DhcpVhostMsg(uint8_t *dhcp, uint32_t length)
            : InterTaskMsg(DHCP_VHOST_MSG), pkt(dhcp), len(length) {}
        virtual ~DhcpVhostMsg() { if (pkt) delete [] pkt; }
    };

    struct DhcpStats {
        DhcpStats() { Reset(); }
        void Reset() {
            discover = request = inform = decline = other =
            offers = acks = nacks = errors = relay_req = relay_resp = 0;
        }

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
    };

    void Shutdown();
    DhcpProto(Agent *agent, boost::asio::io_service &io, bool run_with_vrouter);
    virtual ~DhcpProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void SendDhcpIpc(uint8_t *dhcp, std::size_t len);

    Interface *ip_fabric_interface() const { return ip_fabric_interface_; }
    void set_ip_fabric_interface(Interface *itf) { ip_fabric_interface_ = itf; }
    uint16_t ip_fabric_interface_index() const {
        return ip_fabric_interface_index_;
    }
    void set_ip_fabric_interface_index(uint16_t ind) {
        ip_fabric_interface_index_ = ind;
    }
    const MacAddress &ip_fabric_interface_mac() const {
        return ip_fabric_interface_mac_;
    }
    void set_ip_fabric_interface_mac(const MacAddress &mac) {
        ip_fabric_interface_mac_ = mac;
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
    const DhcpStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    void ItfNotify(DBEntryBase *entry);
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &error, std::size_t len);

    bool run_with_vrouter_;
    Interface *ip_fabric_interface_;
    uint16_t ip_fabric_interface_index_;
    MacAddress ip_fabric_interface_mac_;
    DBTableBase::ListenerId iid_;
    DhcpStats stats_;

    boost::asio::ip::udp::socket dhcp_server_socket_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    uint8_t *dhcp_server_read_buf_;

    DISALLOW_COPY_AND_ASSIGN(DhcpProto);
};

#endif // vnsw_agent_dhcp_proto_hpp
