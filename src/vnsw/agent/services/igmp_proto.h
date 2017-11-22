/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_igmp_proto_hpp
#define vnsw_agent_igmp_proto_hpp

#include "pkt/proto.h"
#include "services/igmp_handler.h"

#define IGMP_PKT_TRACE(obj, arg)                                                 \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Igmp##obj::TraceMsg(IgmpTraceBuf, __FILE__, __LINE__, _str.str());       \
} while (false)                                                              \

class Timer;
class Interface;

class IgmpProto : public Proto {
public:

    struct IgmpStats {
        IgmpStats() { Reset(); }
        void Reset() {
            bad_length = bad_cksum = bad_interface = not_local =
            rx_unknown = rejected_report = 0;
        }

        uint32_t bad_length;
        uint32_t bad_cksum;
        uint32_t bad_interface;
        uint32_t not_local;
        uint32_t rx_unknown;
        uint32_t rejected_report;
    };

    void Shutdown();
    IgmpProto(Agent *agent, boost::asio::io_service &io);
    virtual ~IgmpProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);

    Interface *ip_fabric_interface() const { return ip_fabric_interface_; }
    void set_ip_fabric_interface(Interface *itf) { ip_fabric_interface_ = itf; }
    uint32_t ip_fabric_interface_index() const {
        return ip_fabric_interface_index_;
    }
    void set_ip_fabric_interface_index(uint32_t ind) {
        ip_fabric_interface_index_ = ind;
    }
    uint32_t pkt_interface_index() const {
        return pkt_interface_index_;
    }
    void set_pkt_interface_index(uint32_t ind) {
        pkt_interface_index_ = ind;
    }
    const MacAddress &ip_fabric_interface_mac() const {
        return ip_fabric_interface_mac_;
    }
    void set_ip_fabric_interface_mac(const MacAddress &mac) {
        ip_fabric_interface_mac_ = mac;
    }

    void IncrStatsBadLength() { stats_.bad_length++; }
    void IncrStatsBadCksum() { stats_.bad_cksum++; }
    void IncrStatsBadInterface() { stats_.bad_interface++; }
    void IncrStatsNotLocal() { stats_.not_local++; }
    void IncrStatsRxUnknown() { stats_.rx_unknown++; }
    void IncrStatsRejectedReport() { stats_.rejected_report++; }
    const IgmpStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }

private:
    void ItfNotify(DBEntryBase *entry);
    void VnNotify(DBEntryBase *entry);
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &error, std::size_t len);

    Interface *ip_fabric_interface_;
    uint32_t ip_fabric_interface_index_;
    uint32_t pkt_interface_index_;
    MacAddress ip_fabric_interface_mac_;
    DBTableBase::ListenerId iid_;
    DBTableBase::ListenerId vnid_;
    IgmpStats stats_;

    std::set<VmInterface *> gw_vmi_list_;
    uint32_t gateway_delete_seqno_;

    DISALLOW_COPY_AND_ASSIGN(IgmpProto);
};

#endif // vnsw_agent_igmp_proto_hpp
