/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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

    DBTableBase::ListenerId iid_;
    DBTableBase::ListenerId vnid_;
    IgmpStats stats_;

    DISALLOW_COPY_AND_ASSIGN(IgmpProto);
};

#endif // vnsw_agent_igmp_proto_hpp
