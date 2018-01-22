/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_igmp_proto_hpp
#define vnsw_agent_igmp_proto_hpp

#include "pkt/proto.h"
#include "services/igmp_handler.h"

#include "services/multicast/gmp_map/gmp_proto.h"

#define IGMP_UNDEFINED              0x10
#define IGMP_MEMBERSHIP_QUERY       0x11
#define IGMP_V1_MEMBERSHIP_REPORT   0x12
#define IGMP_PROTO_DVMRP            0x13
#define IGMP_PROTO_PIM              0x14
#define IGMP_CISCO_TRACE            0x15
#define IGMP_V2_MEMBERSHIP_REPORT   0x16
#define IGMP_GROUP_LEAVE            0x17
#define IGMP_MTRACE_RESPONSE        0x1e
#define IGMP_MTRACE_REQUEST         0x1f
#define IGMP_DWR                    0x21
#define IGMP_V3_MEMBERSHIP_REPORT   0x22
#define IGMP_MAX_TYPE               IGMP_V3_MEMBERSHIP_REPORT
#define IGMP_MIN_PACKET_LENGTH      8

#define IGMP_PKT_TRACE(obj, arg)                                                 \
do {                                                                         \
    std::ostringstream _str;                                                 \
    _str << arg;                                                             \
    Igmp##obj::TraceMsg(IgmpTraceBuf, __FILE__, __LINE__, _str.str());       \
} while (false)                                                              \

class Timer;
class Interface;

namespace IgmpInfo {
struct McastInterfaceState : public DBState {
public:
    struct IgmpIfStats {
        IgmpIfStats() { Reset(); }
        void Reset() {
            rx_unknown = 0;
            memset(rx_badpacket, 0x00, sizeof(uint32_t)*IGMP_MAX_TYPE);
            memset(rx_okpacket, 0x00, sizeof(uint32_t)*IGMP_MAX_TYPE);
        }
        uint32_t rx_unknown;
        uint32_t rx_badpacket[IGMP_MAX_TYPE];
        uint32_t rx_okpacket[IGMP_MAX_TYPE];
    };

    McastInterfaceState() {
        gmp_intf_ = NULL;
    }
    ~McastInterfaceState() {}

    void IncrRxUnknown() {
        stats_.rx_unknown++;
    }
    uint32_t GetRxUnknown() {
        return stats_.rx_unknown;
    }
    void IncrRxBadPkt(unsigned long index) {
        stats_.rx_badpacket[index-1]++;
    }
    uint32_t GetRxBadPkt(unsigned long index) {
        return stats_.rx_badpacket[index-1];
    }
    void IncrRxOkPkt(unsigned long index) {
        stats_.rx_okpacket[index-1]++;
    }
    uint32_t GetRxOkPkt(unsigned long index) {
        return stats_.rx_okpacket[index-1];
    }
    const IgmpIfStats &GetIfStats() const { return stats_; }
    void ClearIfStats() { stats_.Reset(); }

    GmpIntf *gmp_intf_;
    IgmpIfStats stats_;
};
}

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

    void IgmpProtoInit(void);

    void IncrStatsBadLength() { stats_.bad_length++; }
    void IncrStatsBadCksum() { stats_.bad_cksum++; }
    void IncrStatsBadInterface() { stats_.bad_interface++; }
    void IncrStatsNotLocal() { stats_.not_local++; }
    void IncrStatsRxUnknown() { stats_.rx_unknown++; }
    void IncrStatsRejectedReport() { stats_.rejected_report++; }
    const IgmpStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }
    GmpProto *GetGmpProto() { return gmp_proto_; }
    const IgmpInfo::McastInterfaceState::IgmpIfStats &GetIfStats(
                            VmInterface *vm_itf);

    DBTableBase::ListenerId ItfListenerId ();

private:
    void ItfNotify(DBTablePartBase *part, DBEntryBase *entry);
    void VrfNotify(DBEntryBase *entry);
    void VnNotify(DBEntryBase *entry);

    void AsyncRead();
    void ReadHandler(const boost::system::error_code &error, std::size_t len);

    const std::string task_name_;
    boost::asio::io_service &io_;

    DBTableBase::ListenerId iid_;

    GmpProto *gmp_proto_;
    IgmpStats stats_;
    int itf_attach_count_;

    DISALLOW_COPY_AND_ASSIGN(IgmpProto);
};

#endif // vnsw_agent_igmp_proto_hpp
