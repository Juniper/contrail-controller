/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_sock_h 
#define ctrlplane_ksync_sock_h 

#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/netlink_protocol.hpp>
#include <boost/asio/netlink_endpoint.hpp>
#include <tbb/atomic.h>
#include <tbb/mutex.h>
#include <base/queue_task.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/common/vns_types.h>
#include <io/tcp_session.h>
#include "vr_types.h"
#include "ksync_tx_queue.h"

#define KSYNC_DEFAULT_MSG_SIZE    4096
#define KSYNC_DEFAULT_Q_ID_SEQ    0x00000001
#define KSYNC_ACK_WAIT_THRESHOLD  200
#define KSYNC_SOCK_RECV_BUFF_SIZE (256 * 1024)

class KSyncEntry;
class KSyncIoContext;
class KSyncSockTcpSession;
struct nl_client;

typedef std::vector<boost::asio::mutable_buffers_1> KSyncBufferList;

/* Base class to hold sandesh context information which is passed to 
 * Sandesh decode
 */
class AgentSandeshContext : public SandeshContext {
public:
    AgentSandeshContext() : errno_(0), ksync_io_ctx_(NULL) { }
    virtual ~AgentSandeshContext() { }

    virtual void IfMsgHandler(vr_interface_req *req) = 0;
    virtual void NHMsgHandler(vr_nexthop_req *req) = 0;
    virtual void RouteMsgHandler(vr_route_req *req) = 0;
    virtual void MplsMsgHandler(vr_mpls_req *req) = 0;
    virtual int VrResponseMsgHandler(vr_response *resp) = 0;
    virtual void MirrorMsgHandler(vr_mirror_req *req) = 0;
    virtual void FlowMsgHandler(vr_flow_req *req) = 0;
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req) = 0;
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req) = 0;
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req) = 0;
    virtual void VxLanMsgHandler(vr_vxlan_req *req) = 0;
    virtual void VrouterOpsMsgHandler(vrouter_ops *req) = 0;
    virtual void SetErrno(int err) {errno_ = err;}

    int GetErrno() const {return errno_;}
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
    static const char *io_wq_names[MAX_WORK_QUEUES];

    IoContext() :
        sandesh_context_(NULL), msg_(NULL), msg_len_(0), seqno_(0),
        work_q_id_(DEFAULT_Q_ID) {
    }
    IoContext(char *msg, uint32_t len, uint32_t seq, AgentSandeshContext *ctx, 
              IoContextWorkQId id) :
        sandesh_context_(ctx), msg_(msg), msg_len_(len), seqno_(seq),
        work_q_id_(id) {
    }
    virtual ~IoContext() { 
        if (msg_ != NULL)
            free(msg_);
    }

    bool operator<(const IoContext &rhs) const {
        return seqno_ < rhs.seqno_;
    }

    virtual void Handler() {}
    virtual void ErrorHandler(int err) {}

    AgentSandeshContext *GetSandeshContext() { return sandesh_context_; }
    IoContextWorkQId GetWorkQId() { return work_q_id_; }

    void SetSeqno(uint32_t seqno) {seqno_ = seqno;}
    uint32_t GetSeqno() const {return seqno_;}
    char *GetMsg() const { return msg_; }
    uint32_t GetMsgLen() const { return msg_len_; }

    boost::intrusive::list_member_hook<> node_;

protected:
    AgentSandeshContext *sandesh_context_;

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
    virtual ~KSyncIoContext() { }

    virtual void Handler();

    void ErrorHandler(int err);
    KSyncEntry *GetKSyncEntry() const {return entry_;}
    KSyncEntry::KSyncEvent event() const {return event_;}
private:
    KSyncEntry *entry_;
    KSyncEntry::KSyncEvent event_;
};

typedef boost::intrusive::member_hook<IoContext,
        boost::intrusive::list_member_hook<>,
        &IoContext::node_> KSyncSockNode;
typedef boost::intrusive::list<IoContext, KSyncSockNode> IoContextList;

/*
 * SandeshContext implementation to handle IoContextList
 *
 * When we do bulk sandesh messaging, netlink request and response will contain
 * more than one sandesh message. KSyncBulkSandeshContext is used to,
 * - Build the bulk context before sending. The list of IoContext bunched is
 *   stored in IoContextList
 * - Maps the sanesh response to the right IoContext in the list
 *
 * The KSync entries are bunched from KSyncTxQueue. The bunching
 * is capped by,
 * - Number of KSync entries
 * - Size of buffer
 * Bunching is also limited by the task spawned for the WorkQueue. When task
 * exits, all entries pending bunching are sent to VRouter.
 *
 * When response is received from VRouter we assume following,
 * - The sequence of response is same as sequence of IoContext entries
 * - Response for each IoContext can itself be more than one Sandesh Response.
 * - Response for each IoContext starts with VrResponseMsg followed optionally
 *   by other response
 *
 * In KSyncBulkSandeshContext, we store IoContextList and iterate thru the
 * responses seeking VrResponseMsg. For ever VrResponseMsg, we take-out one
 * IoContext from the list
 */
class KSyncBulkSandeshContext : public AgentSandeshContext {
public:
    KSyncBulkSandeshContext(IoContext::IoContextWorkQId io_context_type);
    KSyncBulkSandeshContext(const KSyncBulkSandeshContext &rhs);
    virtual ~KSyncBulkSandeshContext();

    void IfMsgHandler(vr_interface_req *req);
    void NHMsgHandler(vr_nexthop_req *req);
    void RouteMsgHandler(vr_route_req *req);
    void MplsMsgHandler(vr_mpls_req *req);
    int VrResponseMsgHandler(vr_response *resp);
    void MirrorMsgHandler(vr_mirror_req *req);
    void FlowMsgHandler(vr_flow_req *req);
    void VrfAssignMsgHandler(vr_vrf_assign_req *req);
    void VrfStatsMsgHandler(vr_vrf_stats_req *req);
    void DropStatsMsgHandler(vr_drop_stats_req *req);
    void VxLanMsgHandler(vr_vxlan_req *req);
    void VrouterOpsMsgHandler(vrouter_ops *req);
    void SetErrno(int err);

    bool Decoder(char *buff, uint32_t buff_len, uint32_t alignment, bool more);
    AgentSandeshContext *GetSandeshContext();
    void IoContextStart();
    void IoContextDone();
    void Insert(IoContext *ioc);
    void Data(KSyncBufferList *iovec);
    IoContext::IoContextWorkQId io_context_type() const {
        return io_context_type_;
    }
    void set_io_context_type(IoContext::IoContextWorkQId io_context_type) {
        io_context_type_ = io_context_type;
    }
private:

    // Number of VrResponseMsg seen
    uint32_t vr_response_count_;
    // Iterator to IoContext being processed
    IoContextList::iterator io_context_list_it_;
    // List of IoContext to be processed in this context
    IoContextList io_context_list_;
    IoContext::IoContextWorkQId io_context_type_;
};

class KSyncSock {
public:
    const static int kMsgGrowSize = 16;
    const static unsigned kBufLen = (4*1024);

    // Number of messages that can be bunched together
    const static unsigned kMaxBulkMsgCount = 16;
    // Max size of buffer that can be bunched together
    const static unsigned kMaxBulkMsgSize = (4*1024);

    typedef std::map<int, KSyncBulkSandeshContext> WaitTree;
    typedef std::pair<int, KSyncBulkSandeshContext> WaitTreePair;
    typedef boost::function<void(const boost::system::error_code &, size_t)>
        HandlerCb;

    KSyncSock();
    virtual ~KSyncSock();

    // Virtual methods
    virtual bool BulkDecoder(char *data, KSyncBulkSandeshContext *ctxt) = 0;
    virtual bool Decoder(char *data, AgentSandeshContext *ctxt) = 0;

    // Write a KSyncEntry to kernel
    void SendAsync(KSyncEntry *entry, int msg_len, char *msg,
                   KSyncEntry::KSyncEvent event);
    std::size_t BlockingSend(char *msg, int msg_len);
    void GenericSend(IoContext *ctx);
    bool BlockingRecv();
    int AllocSeqNo(bool is_uve);

    // Bulk Messaging methods
    KSyncBulkSandeshContext *LocateBulkContext(uint32_t seqno,
                             IoContext::IoContextWorkQId io_context_type);
    int SendBulkMessage(KSyncBulkSandeshContext *bulk_context, uint32_t seqno);
    bool TryAddToBulk(KSyncBulkSandeshContext *bulk_context, IoContext *ioc);
    void OnEmptyQueue(bool done);

    // Start Ksync Asio operations
    static void Start(bool read_inline);
    static void Shutdown();

    // Partition to KSyncSock mapping
    static KSyncSock *Get(DBTablePartBase *partition);
    static KSyncSock *Get(int partition_id);

    static uint32_t GetPid() {return pid_;};
    static int GetNetlinkFamilyId() {return vnsw_netlink_family_id_;}
    static void SetNetlinkFamilyId(int id);

    static AgentSandeshContext *GetAgentSandeshContext() {
        return agent_sandesh_ctx_;
    }
    static void SetAgentSandeshContext(AgentSandeshContext *ctx) {
        agent_sandesh_ctx_ = ctx;
    }
protected:
    static void Init(bool use_work_queue);
    static void SetSockTableEntry(KSyncSock *sock);
    bool ValidateAndEnqueue(char *data);

    nl_client *nl_client_;
    // Tree of all KSyncEntries pending ack from Netlink socket
    WaitTree wait_tree_;
    KSyncTxQueue send_queue_;
    tbb::mutex mutex_;
    WorkQueue<char *> *receive_work_queue[IoContext::MAX_WORK_QUEUES];

    // Information maintained for bulk processing

    // Max messages in one bulk context
    uint32_t max_bulk_msg_count_;
    // Max buffer size in one bulk context
    uint32_t max_bulk_buf_size_;

    // Sequence number of first message in bulk context. Entry in WaitTree is
    // added based on this sequence number
    int      bulk_seq_no_;
    // Current buffer size in bulk context
    uint32_t bulk_buf_size_;
    // Current message count in bulk context
    uint32_t bulk_msg_count_;

private:
    friend class KSyncTxQueue;
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb) = 0;
    virtual void AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                             HandlerCb cb) = 0;
    virtual std::size_t SendTo(KSyncBufferList *iovec, uint32_t seq_no) = 0;
    virtual void Receive(boost::asio::mutable_buffers_1) = 0;
    virtual uint32_t GetSeqno(char *data) = 0;
    virtual bool IsMoreData(char *data) = 0;
    virtual bool Validate(char *data) = 0;

    // Read handler registered with boost::asio. Demux done based on seqno_
    void ReadHandler(const boost::system::error_code& error,
                     size_t bytes_transferred);

    // Write handler registered with boost::asio. Demux done based on seqno_
    void WriteHandler(const boost::system::error_code& error,
                      size_t bytes_transferred);

    bool ProcessKernelData(char *data);
    bool SendAsyncImpl(IoContext *ioc);
    bool SendAsyncStart() {
        tbb::mutex::scoped_lock lock(mutex_);
        return (wait_tree_.size() <= KSYNC_ACK_WAIT_THRESHOLD);
    }

private:
    char *rx_buff_;
    tbb::atomic<int> seqno_;
    tbb::atomic<int> uve_seqno_;

    // Debug stats
    int tx_count_;
    int ack_count_;
    int err_count_;
    bool read_inline_;

    static std::auto_ptr<KSyncSock> sock_;
    static pid_t pid_;
    static int vnsw_netlink_family_id_;
    static AgentSandeshContext *agent_sandesh_ctx_;
    static tbb::atomic<bool> shutdown_;

    DISALLOW_COPY_AND_ASSIGN(KSyncSock);
};

//netlink socket class for interacting with kernel
class KSyncSockNetlink : public KSyncSock {
public:
    KSyncSockNetlink(boost::asio::io_service &ios, int protocol);
    virtual ~KSyncSockNetlink();

    virtual uint32_t GetSeqno(char *data);
    virtual bool IsMoreData(char *data);
    virtual bool BulkDecoder(char *data, KSyncBulkSandeshContext *ctxt);
    virtual bool Decoder(char *data, AgentSandeshContext *ctxt);
    virtual bool Validate(char *data);
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb);
    virtual void AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                             HandlerCb cb);
    virtual std::size_t SendTo(KSyncBufferList *iovec, uint32_t seq_no);
    virtual void Receive(boost::asio::mutable_buffers_1);

    static void NetlinkDecoder(char *data, SandeshContext *ctxt);
    static void NetlinkBulkDecoder(char *data, SandeshContext *ctxt, bool more);
    static void Init(boost::asio::io_service &ios, int protocol);
private:
    boost::asio::netlink::raw::socket sock_;
};

//udp socket class for interacting with user vrouter
class KSyncSockUdp : public KSyncSock {
public:
    KSyncSockUdp(boost::asio::io_service &ios, int port);
    virtual ~KSyncSockUdp() { }

    virtual uint32_t GetSeqno(char *data);
    virtual bool IsMoreData(char *data);
    virtual bool BulkDecoder(char *data, KSyncBulkSandeshContext *ctxt);
    virtual bool Decoder(char *data, AgentSandeshContext *ctxt);
    virtual bool Validate(char *data);
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb);
    virtual void AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                             HandlerCb cb);
    virtual std::size_t SendTo(KSyncBufferList *iovec, uint32_t seq_no);
    virtual void Receive(boost::asio::mutable_buffers_1);

    static void Init(boost::asio::io_service &ios, int port);
private:
    boost::asio::ip::udp::socket sock_;
    boost::asio::ip::udp::endpoint server_ep_;
};

class KSyncSockTcpSessionReader : public TcpMessageReader {
public:
     KSyncSockTcpSessionReader(TcpSession *session, ReceiveCallback callback);
     virtual ~KSyncSockTcpSessionReader() { }

protected:
    virtual int MsgLength(Buffer buffer, int offset);

    virtual const int GetHeaderLenSize() {
        return sizeof(struct nlmsghdr);
    }

    virtual const int GetMaxMessageSize() {
        return kMaxMessageSize;
    }

private:
    static const int kMaxMessageSize = 4096;
};

class KSyncSockTcpSession : public TcpSession {
public:
    KSyncSockTcpSession(TcpServer *server, Socket *sock,
                        bool async_ready = false);
protected:
    virtual void OnRead(Buffer buffer);
private:
    KSyncSockTcpSessionReader *reader_;
};

class KSyncSockTcp : public KSyncSock, public TcpServer {
public:
    KSyncSockTcp(EventManager *evm, boost::asio::ip::address ip_addr,
                 int port);
    virtual ~KSyncSockTcp() { }

    virtual uint32_t GetSeqno(char *data);
    virtual bool IsMoreData(char *data);
    virtual bool BulkDecoder(char *data, KSyncBulkSandeshContext *ctxt);
    virtual bool Decoder(char *data, AgentSandeshContext *ctxt);
    virtual bool Validate(char *data);
    virtual void AsyncReceive(boost::asio::mutable_buffers_1, HandlerCb);
    virtual void AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                             HandlerCb cb);
    virtual std::size_t SendTo(KSyncBufferList *iovec, uint32_t seq_no);
    virtual void Receive(boost::asio::mutable_buffers_1);
    virtual TcpSession *AllocSession(Socket *socket);

    bool ReceiveMsg(const u_int8_t *msg, size_t size);
    void OnSessionEvent(TcpSession *session, TcpSession::Event event);
    bool connect_complete() const {
        return connect_complete_;
    }
    void AsyncReadStart();

    static void Init(EventManager *evm,
                     boost::asio::ip::address ip_addr, int port);
private:
    EventManager *evm_;
    TcpSession *session_;
    boost::asio::ip::tcp::endpoint server_ep_;
    bool connect_complete_;
};
#endif // ctrlplane_ksync_sock_h
