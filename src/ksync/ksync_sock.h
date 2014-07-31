/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_sock_h 
#define ctrlplane_ksync_sock_h 

#include <vector>
#if defined(__linux__)
#include <linux/rtnetlink.h>
#endif
#include <boost/asio/netlink_protocol.hpp>
#include <boost/asio/netlink_endpoint.hpp>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <tbb/atomic.h>
#include <tbb/mutex.h>
#include <base/queue_task.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/common/vns_types.h>
#include "vr_types.h"

#define KSYNC_DEFAULT_MSG_SIZE    4096
#define KSYNC_DEFAULT_Q_ID_SEQ    0x00000001
#define KSYNC_ACK_WAIT_THRESHOLD  200
#define KSYNC_SOCK_RECV_BUFF_SIZE (256 * 1024)

class KSyncEntry;
class KSyncIoContext;

/* Base class to hold sandesh context information which is passed to 
 * Sandesh decode
 */
class AgentSandeshContext : public SandeshContext {
public:
    AgentSandeshContext() : errno_(0) { };
    virtual ~AgentSandeshContext() { };

    virtual void IfMsgHandler(vr_interface_req *req) = 0;
    virtual void NHMsgHandler(vr_nexthop_req *req) = 0;
    virtual void RouteMsgHandler(vr_route_req *req) = 0;
    virtual void MplsMsgHandler(vr_mpls_req *req) = 0;
    virtual int VrResponseMsgHandler(vr_response *r) = 0;
    virtual void MirrorMsgHandler(vr_mirror_req *req) = 0;
    virtual void FlowMsgHandler(vr_flow_req *req) = 0;
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req) = 0;
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req) = 0;
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req) = 0;
    virtual void VxLanMsgHandler(vr_vxlan_req *req) = 0;

    void SetErrno(int err) {errno_ = err;};
    int GetErrno() const {return errno_;};

    void set_ksync_io_ctx(const KSyncIoContext *ioc) {ksync_io_ctx_ = ioc;}
    const KSyncIoContext *ksync_io_ctx() const {return ksync_io_ctx_;}
private:
    int errno_;
    const KSyncIoContext *ksync_io_ctx_;
};


/* Base class for context management. Used while sending and 
 * receiving data via ksync socket
 */
class  IoContext {
public:
    enum IoContextWorkQId {
        DEFAULT_Q_ID,
        UVE_Q_ID,
        MAX_WORK_QUEUES // This should always be last
    };
    static const char* io_wq_names[MAX_WORK_QUEUES];
    IoContext() : ctx_(NULL), msg_(NULL), msg_len_(0), seqno_(0) { };

    IoContext(char *msg, uint32_t len, uint32_t seq, AgentSandeshContext *ctx) 
        : ctx_(ctx), msg_(msg), msg_len_(len), seqno_(seq), 
          work_q_id_(DEFAULT_Q_ID) { };
    IoContext(char *msg, uint32_t len, uint32_t seq, AgentSandeshContext *ctx, 
              IoContextWorkQId id) : ctx_(ctx), msg_(msg), msg_len_(len), 
              seqno_(seq), work_q_id_(id) { };
    virtual ~IoContext() { 
        if (msg_ != NULL)
            free(msg_);
    };

    bool operator<(const IoContext &rhs) const {
        return seqno_ < rhs.seqno_;
    };

    void SetSeqno(uint32_t seqno) {seqno_ = seqno;};
    uint32_t GetSeqno() const {return seqno_;};

    virtual void Handler() {};
    virtual void ErrorHandler(int err) {};

    AgentSandeshContext *GetSandeshContext() { return ctx_; }
    IoContextWorkQId GetWorkQId() { return work_q_id_; }

    char *GetMsg() { return msg_; }
    uint32_t GetMsgLen() { return msg_len_; }

    boost::intrusive::set_member_hook<> node_;

protected:
    AgentSandeshContext *ctx_;

private:
    char *msg_;
    uint32_t msg_len_;
    uint32_t seqno_;
    IoContextWorkQId work_q_id_;

    friend class KSyncSock;
};

/* IoContext tied to KSyncEntry */
class  KSyncIoContext : public IoContext {
public:
    KSyncIoContext(KSyncEntry *sync_entry, int msg_len, char *msg,
                   uint32_t seqno, KSyncEntry::KSyncEvent event);
    virtual void Handler();
    void ErrorHandler(int err);
    const KSyncEntry *GetKSyncEntry() const {return entry_;};
    KSyncEntry::KSyncEvent event() const {return event_;}
private:
    KSyncEntry *entry_;
    KSyncEntry::KSyncEvent event_;
};

typedef boost::intrusive::member_hook<IoContext,
        boost::intrusive::set_member_hook<>,
        &IoContext::node_> KSyncSockNode;
typedef boost::intrusive::set<IoContext, KSyncSockNode> Tree;

class KSyncSock {
public:
    const static int kMsgGrowSize = 16;
    const static unsigned kBufLen = 4096;

    typedef boost::function<void(const boost::system::error_code &, size_t)> HandlerCb;
    KSyncSock();
    virtual ~KSyncSock();

    // Start Ksync Asio operations
    static void Start(bool run_sync_mode);
    static void Shutdown();

    // Partition to KSyncSock mapping
    static KSyncSock *Get(DBTablePartBase *partition);
    static KSyncSock *Get(int partition_id);
    // Write a KSyncEntry to kernel
    void SendAsync(KSyncEntry *entry, int msg_len, char *msg, KSyncEntry::KSyncEvent event);
    std::size_t BlockingSend(const char *msg, int msg_len);
    bool BlockingRecv();

    static uint32_t GetPid() {return pid_;};
    static int GetNetlinkFamilyId() {return vnsw_netlink_family_id_;};
    static void SetNetlinkFamilyId(int id) {vnsw_netlink_family_id_ = id;};
    int AllocSeqNo(bool is_uve) { 
        int seq;
        if (is_uve) {
            seq = uve_seqno_.fetch_and_add(2);
        } else {
            seq = seqno_.fetch_and_add(2);
            seq |= KSYNC_DEFAULT_Q_ID_SEQ;
        }
        return seq;
    }
    void GenericSend(IoContext *ctx);
    static AgentSandeshContext *GetAgentSandeshContext() {
        return agent_sandesh_ctx_;
    }
    static void SetAgentSandeshContext(AgentSandeshContext *ctx) {
        agent_sandesh_ctx_ = ctx;
    }
    virtual void Decoder(char *data, SandeshContext *ctxt) = 0;
protected:
    static void Init(int count);
    static void SetSockTableEntry(int i, KSyncSock *sock);
    // Tree of all KSyncEntries pending ack from Netlink socket
    Tree wait_tree_;
    WorkQueue<IoContext *> *async_send_queue_;
    tbb::mutex mutex_;

    WorkQueue<char *> *receive_work_queue[IoContext::MAX_WORK_QUEUES];
private:
    // Read handler registered with boost::asio. Demux done based on seqno_
    void ReadHandler(const boost::system::error_code& error,
                     size_t bytes_transferred);

    // Write handler registered with boost::asio. Demux done based on seqno_
    void WriteHandler(const boost::system::error_code& error,
                      size_t bytes_transferred);

    bool ProcessKernelData(char *data);
    virtual bool Validate(char *data) = 0;
    bool ValidateAndEnqueue(char *data);
    bool SendAsyncImpl(IoContext *ioc);

    bool SendAsyncStart() {
        tbb::mutex::scoped_lock lock(mutex_);
        return (wait_tree_.size() <= KSYNC_ACK_WAIT_THRESHOLD);
    }

    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb) = 0;
    virtual void AsyncSendTo(IoContext *, boost::asio::mutable_buffers_1,
                             HandlerCb) = 0;
    virtual std::size_t SendTo(boost::asio::const_buffers_1, uint32_t) = 0;
    virtual void Receive(boost::asio::mutable_buffers_1) = 0;

    virtual uint32_t GetSeqno(char *data) = 0;
    Tree::iterator GetIoContext(char *data);
    virtual bool IsMoreData(char *data) = 0;

    static std::vector<KSyncSock *> sock_table_;
    static pid_t pid_;
    static int vnsw_netlink_family_id_;
    static AgentSandeshContext *agent_sandesh_ctx_;
    static tbb::atomic<bool> shutdown_;

    char *rx_buff_;
    tbb::atomic<int> seqno_;
    tbb::atomic<int> uve_seqno_;

    // Debug stats
    int tx_count_;
    int ack_count_;
    int err_count_;
    bool run_sync_mode_;

    DISALLOW_COPY_AND_ASSIGN(KSyncSock);
};

//netlink socket class for interacting with kernel
class KSyncSockNetlink : public KSyncSock {
public:
    KSyncSockNetlink(boost::asio::io_service &ios, int protocol);
    virtual ~KSyncSockNetlink() { };

    static void Init(boost::asio::io_service &ios, int count, int protocol);
    virtual uint32_t GetSeqno(char *data);
    virtual bool IsMoreData(char *data);
    virtual void Decoder(char *data, SandeshContext *ctxt);
    virtual bool Validate(char *data);
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb);
    virtual void AsyncSendTo(IoContext *, boost::asio::mutable_buffers_1,
                             HandlerCb);
    virtual std::size_t SendTo(boost::asio::const_buffers_1, uint32_t);
    virtual void Receive(boost::asio::mutable_buffers_1);
private:
    boost::asio::netlink::raw::socket sock_;
};

//udp socket class for interacting with user vrouter
class KSyncSockUdp : public KSyncSock {
public:
    KSyncSockUdp(boost::asio::io_service &ios, int port);
    virtual ~KSyncSockUdp() { };

    static void Init(boost::asio::io_service &ios, int count, int port);
    virtual uint32_t GetSeqno(char *data);
    virtual bool IsMoreData(char *data);
    virtual void Decoder(char *data, SandeshContext *ctxt);
    virtual bool Validate(char *data);
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb);
    virtual void AsyncSendTo(IoContext *, boost::asio::mutable_buffers_1,
                             HandlerCb);
    virtual std::size_t SendTo(boost::asio::const_buffers_1, uint32_t);
    virtual void Receive(boost::asio::mutable_buffers_1);
private:
    boost::asio::ip::udp::socket sock_;
    boost::asio::ip::udp::endpoint server_ep_;
};

#endif // ctrlplane_ksync_sock_h
