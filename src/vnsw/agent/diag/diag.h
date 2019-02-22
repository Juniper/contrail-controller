/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_diag_hpp
#define vnsw_agent_diag_diag_hpp

#include <base/logging.h>
#include <base/address.h>
#include <base/timer.h>
#include <base/queue_task.h>
#include "boost/date_time/posix_time/posix_time.hpp"

class Agent;
struct AgentDiagPktData;
struct OverlayOamPktData;
class DiagProto;
class DiagTable;
class DiagPktHandler;

class DiagEntry {
public:
    typedef uint16_t DiagKey;
    typedef Timer DiagTimer;

    DiagEntry(const std::string &sip, const std::string &dip, uint8_t proto,
              uint16_t sport, uint16_t dport, const std::string &vrf_name,
              int timeout, int count, DiagTable *diag_table);
    virtual ~DiagEntry();
    void Init();
    void EnqueueForceDelete();
    virtual void SendRequest() = 0;
    virtual void HandleReply(DiagPktHandler *handler) = 0;
    virtual void RequestTimedOut(uint32_t seq_no) = 0;
    virtual void SendSummary() { };
    bool TimerExpiry(uint32_t seqno);
    void RestartTimer();
    virtual bool IsDone();
    virtual bool ResendOnTimerExpiry() { return true; }
    DiagKey GetKey() { return key_;};
    uint32_t GetSeqNo() {return seq_no_;};
    uint32_t GetMaxAttempts() {return max_attempts_;};
    void SetKey(DiagKey key) {key_ = key;};
    virtual void Retry();
    bool TimerCancel() {
        return timer_->Cancel();
    }
    DiagTable* diag_table() const {
        return diag_table_;
    }
    uint32_t HashValUdpSourcePort();
    void FillOamPktHeader(OverlayOamPktData *pktdata, uint32_t vxlan_id,
                          const boost::posix_time::ptime &time);

protected:
    IpAddress sip_;
    IpAddress dip_;
    uint8_t    proto_;
    uint16_t   sport_;
    uint16_t   dport_;
    std::string vrf_name_;
    boost::system::error_code ec_;

    DiagTable *diag_table_;
    DiagKey key_;

    int timeout_;
    DiagTimer *timer_;
    uint32_t max_attempts_;
    uint32_t seq_no_;
};

struct AgentDiagPktData {
    enum Op {
        DIAG_REQUEST = 1,
        DIAG_REPLY
    };

    AgentDiagPktData() {
        memset(data_, 0, sizeof(data_));
    }
    uint32_t op_;
    DiagEntry::DiagKey key_;
    uint32_t seq_no_;
    char data_[8];
    boost::posix_time::ptime rtt_;
};

struct DiagEntryOp {
    enum Op {
        ADD = 1,
        DEL,
        RETRY,
        FORCE_DELETE
    };

    DiagEntryOp(DiagEntryOp::Op op, DiagEntry *de): op_(op), de_(de) {
    }

    Op op_;
    DiagEntry *de_;
};

class DiagTable {
public:
    typedef std::map<DiagEntry::DiagKey, DiagEntry *> DiagEntryTree;
    static const std::string kDiagData;
    DiagTable(Agent *agent);
    ~DiagTable();
    void Add(DiagEntry *);
    void Delete(DiagEntry *);
    DiagEntry* Find(DiagEntry::DiagKey &key);

    void Shutdown();
    void Enqueue(DiagEntryOp *op);
    bool Process(DiagEntryOp *op);
    Agent* agent() const { return agent_; }
    DiagProto *diag_proto() const { return diag_proto_.get(); }

private:
    uint16_t index_;
    DiagEntryTree tree_;
    boost::scoped_ptr<DiagProto> diag_proto_;
    WorkQueue<DiagEntryOp *> *entry_op_queue_;
    Agent *agent_;
};

struct OamTlv {
    enum Type {
        VXLAN_PING_IPv4 = 1,
        VXLAN_PING_IPv6 = 2,
        NVGRE_PING_IPv4 = 3,
        NVGRE_PING_IPv6 = 4,
        MPLSoGRE_PING_IPv4 = 5,
        MPLSoGRE_PING_IPv6 = 6,
        MPLSoUDP_PING_IPv4 = 7,
        MPLSoUDP_PING_IPv6 = 8,
    };

    struct VxlanOamV4Tlv {
        uint32_t vxlan_id_;
        uint32_t sip_;
    };

    struct VxlanOamV6Tlv {
        uint32_t vxlan_id_;
        uint8_t  sip_[16];
    };

    uint16_t type_;
    uint16_t length_;
    char data_[1];
};

struct SubTlv {
    enum Type {
        END_SYSTEM_MAC = 1,
        END_SYSTEM_IPv4 = 2,
        END_SYSTEM_IPv6 = 3,
        END_SYSTEM_MAC_IPv4 = 4,
        END_SYSTEM_MAC_IPv6 = 5,
    };

    enum ReturnCode {
        END_SYSTEM_PRESENT = 1,
        END_SYSTEM_NOT_PRESENT = 2,
    };

    uint16_t type_;
    uint16_t length_;

    struct EndSystemMac {
        uint8_t mac[6];
        uint16_t return_code;
    };
};

struct OverlayOamPktData {
   enum MsgType {
    OVERLAY_ECHO_REQUEST = 1,
    OVERLAY_ECHO_REPLY = 2
   };

   enum Returncode {
    NO_RETURN_CODE = 0,
    MALFORMED_ECHO_REQUEST = 1,
    OVERLAY_SEGMENT_NOT_PRESENT = 2,
    OVERLAY_SEGMENT_NOT_OPERATIONAL = 3,
    RETURN_CODE_OK = 4,
   };

   enum Replymode {
    DONT_REPLY = 1,
    REPLY_IPV4ORV6_UDP = 2,
    REPLY_OVERLAY_SEGMENT = 3,
   };

   uint8_t msg_type_;
   uint8_t reply_mode_;
   uint8_t return_code_;
   uint8_t return_subcode_;
   uint32_t org_handle_;
   uint32_t seq_no_;
   uint32_t timesent_sec_;
   uint32_t timesent_misec_;
   uint32_t timerecv_sec_;
   uint32_t timerecv_misec_;
   OamTlv oamtlv_;
};
#endif
