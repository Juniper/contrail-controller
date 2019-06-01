/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_ksync_sock_h
#define ctrlplane_ksync_sock_h

#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>

#ifndef _WIN32
#include <boost/asio/netlink_protocol.hpp>
#include <boost/asio/netlink_endpoint.hpp>
#endif

#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include <base/queue_task.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/common/vns_types.h>
#include <io/tcp_session.h>
#include <vr_types.h>
#include <nl_util.h>
#include "ksync_entry.h"
#include "ksync_tx_queue.h"

#define KSYNC_DEFAULT_MSG_SIZE    4096
#define KSYNC_DEFAULT_Q_ID_SEQ    0x00000001
#define KSYNC_ACK_WAIT_THRESHOLD  200
#define KSYNC_SOCK_RECV_BUFF_SIZE (256 * 1024)
#define KSYNC_BMC_ARR_SIZE        1024

class KSyncEntry;
class KSyncIoContext;
class KSyncSockTcpSession;
struct nl_client;
class KSyncBulkSandeshContext;

typedef std::vector<boost::asio::mutable_buffers_1> KSyncBufferList;

uint32_t GetNetlinkSeqno(char *data);
bool NetlinkMsgDone(char *data);
bool ValidateNetlink(char *data);
void GetNetlinkPayload(char *data, char **buf, uint32_t *buf_len);
void InitNetlink(nl_client *client);
void ResetNetlink(nl_client *client);
void UpdateNetlink(nl_client *client, uint32_t len, uint32_t seq_no);
void DecodeSandeshMessages(char *buf, uint32_t buf_len, SandeshContext *sandesh_context,
                           uint32_t alignment);

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
    virtual void FlowResponseHandler(vr_flow_response *req) { assert(0); }
    virtual void FlowTableInfoHandler(vr_flow_table_data *r) { assert(0); }
    virtual void BridgeTableInfoHandler(vr_bridge_table_data *r) { assert(0);}
    virtual void VrfAssignMsgHandler(vr_vrf_assign_req *req) = 0;
    virtual void VrfStatsMsgHandler(vr_vrf_stats_req *req) = 0;
    virtual void DropStatsMsgHandler(vr_drop_stats_req *req) = 0;
    virtual void VxLanMsgHandler(vr_vxlan_req *req) = 0;
    virtual void VrouterHugePageHandler(vr_hugepage_config *req) {}
    virtual void VrouterOpsMsgHandler(vrouter_ops *req) = 0;
    virtual void QosConfigMsgHandler(vr_qos_map_req *req) = 0;
    virtual void ForwardingClassMsgHandler(vr_fc_map_req *req) = 0;
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
    // Type of IoContext. The work-queue used for processing response is based
    // on this
    // IOC_UVE : Used for UVEs
    // IOC_KSYNC : Used for KSync objects
    enum Type {
        IOC_UVE,
        IOC_KSYNC,
        MAX_WORK_QUEUES // This should always be last
    };
    // Work-queue neames used for the ksync receive work-queues
    static const char *io_wq_names[MAX_WORK_QUEUES];

    IoContext() :
        sandesh_context_(NULL), msg_(NULL), msg_len_(0), seqno_(0),
        type_(IOC_KSYNC), index_(0), rx_buffer1_(NULL), rx_buffer2_(NULL) {
    }
    IoContext(char *msg, uint32_t len, uint32_t seq, AgentSandeshContext *ctx,
              Type type) :
        sandesh_context_(ctx), msg_(msg), msg_len_(len), seqno_(seq),
        type_(type), index_(0), rx_buffer1_(NULL), rx_buffer2_(NULL) {
    }
    IoContext(char *msg, uint32_t len, uint32_t seq, AgentSandeshContext *ctx,
              Type type, uint32_t index) :
        sandesh_context_(ctx), msg_(msg), msg_len_(len), seqno_(seq),
        type_(type), index_(index), rx_buffer1_(NULL), rx_buffer2_(NULL) {
    }
    virtual ~IoContext() {
        if (msg_ != NULL)
            free(msg_);
        assert(rx_buffer1_ == NULL);
        assert(rx_buffer2_ == NULL);
    }

    bool operator<(const IoContext &rhs) const {
        return seqno_ < rhs.seqno_;
    }

    virtual void Handler() {}
    virtual void ErrorHandler(int err) {}

    AgentSandeshContext *GetSandeshContext() { return sandesh_context_; }
    Type type() { return type_; }

    void SetSeqno(uint32_t seqno) {seqno_ = seqno;}
    uint32_t GetSeqno() const {return seqno_;}
    char *GetMsg() const { return msg_; }
    uint32_t GetMsgLen() const { return msg_len_; }
    char *rx_buffer1() const { return rx_buffer1_; }
    void reset_rx_buffer1() { rx_buffer1_ = NULL; }
    char *rx_buffer2() { return rx_buffer2_; }
    void reset_rx_buffer2() { rx_buffer2_ = NULL; }
    uint32_t index() const { return index_; }

    boost::intrusive::list_member_hook<> node_;

protected:
    AgentSandeshContext *sandesh_context_;

private:
    char *msg_;
    uint32_t msg_len_;
    uint32_t seqno_;
    Type type_;
    uint32_t index_;
    // Buffers allocated to read the ksync responses for this IoContext.
    // As an optimization, KSync Tx Queue will use these buffers to minimize
    // computation in KSync Tx Queue context.
    char *rx_buffer1_;
    char *rx_buffer2_;

    friend class KSyncSock;
};

/* IoContext tied to KSyncEntry */
class  KSyncIoContext : public IoContext {
public:
    KSyncIoContext(KSyncSock *sock, KSyncEntry *sync_entry, int msg_len,
                   char *msg, KSyncEntry::KSyncEvent event);
    virtual ~KSyncIoContext() {
    }

    virtual void Handler();

    void ErrorHandler(int err);
    KSyncEntry *GetKSyncEntry() const {return entry_;}
    KSyncEntry::KSyncEvent event() const {return event_;}
private:
    KSyncEntry *entry_;
    KSyncEntry::KSyncEvent event_;
    AgentSandeshContext *agent_sandesh_ctx_;
    KSyncSock *sock_;
};

/*
 * KSync implementation of bunching messages.
 *
 * Message bunching has two parts,
 * Encoding messages
 *   KSyncTxQueue is responsible to bunch KSync requests into single message
 *
 *   KSyncTxQueue uses KSyncBulkMsgContext to bunch KSync events.
 *   The IoContext for bunched KSync events are stored as list inside
 *   KSyncBulkMessageContext
 *
 *   KSync Events are bunched if pass following constraints,
 *   - Number of KSync events is less than max_bulk_msg_count_
 *   - Total size of buffer for events is less than max_bulk_buf_size_
 *   - Type of IoContext is same.
 *     When type of IoContext is same, it also ensures that KSync Responses
 *     are processed in same WorkQueue.
 *   - Each IoContext type has multiple work-queues. Flows are sprayed across
 *     the work-queues based on flow-table index. Entries are bunched only if
 *     they belong to same work-queue.
 *     This also ensures that flows from a flow-table partition are processed
 *     in single KSync Response Work-Queue
 *   - The input queue for KSyncTxQueue is not empty.
 *
 * Decoding messages
 *   KSync response are processed in context of KSyncSock::receive_work_queue_.
 *   Each IoContext type has its own set of receive work-queue.
 *
 *   Bulk decoder works on following assumption,
 *   - Expects KSync Responses for each IoContext in KSyncBulkMsgContext
 *   - Response for each IoContext can itself be more than one Sandesh Response.
 *   - The sequence of response is same as sequence of IoContext entries
 *   - Response for each IoContext starts with VrResponseMsg followed
 *     optionally by other response. Hence, decoder moves to next IoContext
 *     after getting VrResponse message
 *
 *     class KSyncBulkSandeshContext is used to decode the Sandesh Responses
 *     and move the IoContext on getting VrResponse
 */
typedef boost::intrusive::member_hook<IoContext,
        boost::intrusive::list_member_hook<>,
        &IoContext::node_> KSyncSockNode;
typedef boost::intrusive::list<IoContext, KSyncSockNode> IoContextList;

class KSyncBulkMsgContext {
public:
    const static unsigned kMaxRxBufferCount = 64;
    KSyncBulkMsgContext(IoContext::Type type, uint32_t index);
    KSyncBulkMsgContext(const KSyncBulkMsgContext &rhs);
    ~KSyncBulkMsgContext();

    void Insert(IoContext *ioc);
    void Data(KSyncBufferList *iovec);
    IoContext::Type io_context_type() const {
        return io_context_type_;
    }
    void AddReceiveBuffer(char *buff);
    char *GetReceiveBuffer();
    uint32_t work_queue_index() const { return work_queue_index_; }
    void set_seqno(uint32_t seq) { seqno_ = seq; }
    uint32_t seqno() { return seqno_; }
private:
    friend class KSyncBulkSandeshContext;
    // List of IoContext to be processed in this context
    IoContextList io_context_list_;
    // Type of message
    IoContext::Type io_context_type_;
    // Index of work-queue
    uint32_t work_queue_index_;
    // List of rx-buffers
    // The buffers are taken from IoContext added to the list above
    // If IoContext does not have buffer, then it dyamically allocates
    // buffer
    char *rx_buffers_[kMaxRxBufferCount];
    // Index of next buffer to process
    uint32_t rx_buffer_index_;

    ///////////////////////////////////////////////////////////////////////
    // Following fields are used for decode processing
    ///////////////////////////////////////////////////////////////////////
    // Number of responses already processed
    uint32_t vr_response_count_;
    // Iterator to IoContext being processed
    IoContextList::iterator io_context_list_it_;
    uint32_t seqno_;
};

class KSyncBulkSandeshContext : public AgentSandeshContext {
public:
    KSyncBulkSandeshContext();
    virtual ~KSyncBulkSandeshContext();

    void IfMsgHandler(vr_interface_req *req);
    void NHMsgHandler(vr_nexthop_req *req);
    void RouteMsgHandler(vr_route_req *req);
    void MplsMsgHandler(vr_mpls_req *req);
    int VrResponseMsgHandler(vr_response *resp);
    void MirrorMsgHandler(vr_mirror_req *req);
    void FlowMsgHandler(vr_flow_req *req);
    void FlowResponseHandler(vr_flow_response *req);
    void VrfAssignMsgHandler(vr_vrf_assign_req *req);
    void VrfStatsMsgHandler(vr_vrf_stats_req *req);
    void DropStatsMsgHandler(vr_drop_stats_req *req);
    void VxLanMsgHandler(vr_vxlan_req *req);
    void VrouterOpsMsgHandler(vrouter_ops *req);
    void QosConfigMsgHandler(vr_qos_map_req *req);
    void ForwardingClassMsgHandler(vr_fc_map_req *req);
    void SetErrno(int err);

    bool Decoder(char *buff, uint32_t buff_len, uint32_t alignment, bool more);
    AgentSandeshContext *GetSandeshContext();
    void set_bulk_message_context(KSyncBulkMsgContext *bulk_context) {
        bulk_msg_context_ = bulk_context;
    }

    void IoContextStart();
    void IoContextDone();

private:
    KSyncBulkMsgContext *bulk_msg_context_;
    DISALLOW_COPY_AND_ASSIGN(KSyncBulkSandeshContext);
};

class KSyncSock {
public:
    // Number of flow receive queues
    const static int kRxWorkQueueCount = 2;
    const static int kMsgGrowSize = 16;
    const static unsigned kBufLen = (4*1024);

    // Number of messages that can be bunched together
    const static unsigned kMaxBulkMsgCount = 16;
    // Max size of buffer that can be bunched together
    const static unsigned kMaxBulkMsgSize = (4*1024);
    // Sequence number to denote invalid builk-context
    const static unsigned kInvalidBulkSeqNo = 0xFFFFFFFF;

    typedef std::map<uint32_t, KSyncBulkMsgContext> WaitTree;
    typedef std::pair<uint32_t, KSyncBulkMsgContext> WaitTreePair;
    typedef boost::function<void(const boost::system::error_code &, size_t)>
        HandlerCb;

    // Request structure in the KSync Response Queue
    struct KSyncRxData {
        // buffer having KSync response
        char *buff_;
        // bulk context for decoding response
        KSyncBulkMsgContext *bulk_msg_context_;

        KSyncRxData() : buff_(NULL), bulk_msg_context_(NULL) { }
        KSyncRxData(const KSyncRxData &rhs) :
            buff_(rhs.buff_), bulk_msg_context_(rhs.bulk_msg_context_) {
        }
        KSyncRxData(char *buff, KSyncBulkMsgContext *ctxt) :
            buff_(buff), bulk_msg_context_(ctxt) {
        }
    };
    typedef WorkQueue<KSyncRxData> KSyncReceiveQueue;
    // structure for ksyncrprocess Rx queue
    struct KSyncRxQueueData {
         KSyncEntry             *entry_;
         KSyncEntry::KSyncEvent event_;
         KSyncRxQueueData():entry_(NULL),event_(KSyncEntry::INVALID) {}
         KSyncRxQueueData(KSyncEntry *entry, KSyncEntry::KSyncEvent event) :
                 entry_(entry), event_(event) {
         }
     };
    typedef WorkQueue<KSyncRxQueueData> KSyncRxWorkQueue;

    KSyncSock();
    virtual ~KSyncSock();

    // Virtual methods
    virtual bool BulkDecoder(char *data, KSyncBulkSandeshContext *ctxt) = 0;
    virtual bool Decoder(char *data, AgentSandeshContext *ctxt) = 0;

    // Write a KSyncEntry to kernel
    void SendAsync(KSyncEntry *entry, int msg_len, char *msg,
                   KSyncEntry::KSyncEvent event);
    std::size_t BlockingSend(char *msg, int msg_len);
    bool BlockingRecv();
    void GenericSend(IoContext *ctx);
    uint32_t AllocSeqNo(IoContext::Type type);
    uint32_t AllocSeqNo(IoContext::Type type, uint32_t instance);
    KSyncReceiveQueue *GetReceiveQueue(IoContext::Type type, uint32_t instance);
    KSyncReceiveQueue *GetReceiveQueue(uint32_t seqno);

    // Bulk Messaging methods
    KSyncBulkMsgContext *LocateBulkContext(uint32_t seqno,
                             IoContext::Type io_context_type,
                             uint32_t work_queue_index);
    int SendBulkMessage(KSyncBulkMsgContext *bulk_context, uint32_t seqno);
    bool TryAddToBulk(KSyncBulkMsgContext *bulk_context, IoContext *ioc);
    void OnEmptyQueue(bool done);
    int tx_count() const { return tx_count_; }

    // Start Ksync Asio operations
    static void Start(bool read_inline);
    static void Shutdown();

    // Partition to KSyncSock mapping
    static KSyncSock *Get(DBTablePartBase *partition);
    static KSyncSock *Get(int partition_id);

    static uint32_t GetPid() {return pid_;};
    static int GetNetlinkFamilyId() {return vnsw_netlink_family_id_;}
    static void SetNetlinkFamilyId(int id);

    static AgentSandeshContext *GetAgentSandeshContext(uint32_t type) {
        return agent_sandesh_ctx_[type % kRxWorkQueueCount];
    }
    static void SetAgentSandeshContext(AgentSandeshContext *ctx, uint32_t idx) {
        agent_sandesh_ctx_[idx] = ctx;
    }

    const KSyncTxQueue *send_queue() const { return &send_queue_; }
    const KSyncReceiveQueue *get_receive_work_queue(uint16_t index) const {
        return ksync_rx_queue[index];
    }
    // Allocate a recieve work-queue

    KSyncReceiveQueue *AllocQueue(KSyncBulkSandeshContext ctxt[],
                                  uint32_t task_id, uint32_t instance,
                                  const char *name);

    uint32_t WaitTreeSize() const;
    void SetSeqno(uint32_t seq);
    void SetMeasureQueueDelay(bool val);
    void reset_use_wait_tree() { use_wait_tree_ = false; }
    void set_process_data_inline() { process_data_inline_ = true; }
    // API to enqueue ksync events to rx process work queue
    void EnqueueRxProcessData(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
protected:
    static void Init(bool use_work_queue, const std::string &cpu_pin_policy);
    static void SetSockTableEntry(KSyncSock *sock);
    bool ValidateAndEnqueue(char *data, KSyncBulkMsgContext *context);
    KSyncBulkSandeshContext *GetBulkSandeshContext(uint32_t seqno);
    void ProcessDataInline(char *data);

    tbb::mutex mutex_;
    nl_client *nl_client_;
    // Tree of all IoContext pending ksync response
    WaitTree wait_tree_;
    KSyncTxQueue send_queue_;
    KSyncReceiveQueue *uve_rx_queue[kRxWorkQueueCount];
    KSyncReceiveQueue *ksync_rx_queue[kRxWorkQueueCount];

    // Information maintained for bulk processing

    // Max messages in one bulk context
    uint32_t max_bulk_msg_count_;
    // Max buffer size in one bulk context
    uint32_t max_bulk_buf_size_;

    // Sequence number of first message in bulk context. Entry in WaitTree is
    // added based on this sequence number
    uint32_t bulk_seq_no_;
    // Current buffer size in bulk context
    uint32_t bulk_buf_size_;
    // Current message count in bulk context
    uint32_t bulk_msg_count_;

    uint32_t bmca_prod_;
    uint32_t bmca_cons_;
    KSyncBulkMsgContext *bulk_mctx_arr_[KSYNC_BMC_ARR_SIZE];

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

    bool ProcessKernelData(KSyncBulkSandeshContext *ksync_context,
                           const KSyncRxData &data);
    bool ProcessRxData(KSyncRxQueueData data);
    bool SendAsyncImpl(IoContext *ioc);
    bool SendAsyncStart() {
        tbb::mutex::scoped_lock lock(mutex_);
        return (wait_tree_.size() <= KSYNC_ACK_WAIT_THRESHOLD);
    }

private:
    char *rx_buff_;
    tbb::atomic<uint32_t> seqno_;
    tbb::atomic<uint32_t> uve_seqno_;
    // Read ksync responses inline
    // The IoContext WaitTree is not used when response is read-inline
    bool read_inline_;
    KSyncBulkMsgContext *bulk_msg_context_;
    bool use_wait_tree_;
    bool process_data_inline_;
    KSyncBulkSandeshContext ksync_bulk_sandesh_context_[kRxWorkQueueCount];
    KSyncBulkSandeshContext uve_bulk_sandesh_context_[kRxWorkQueueCount];

    // Debug stats
    int tx_count_;
    int ack_count_;
    int err_count_;
    
    // IO context can defer ksync event processing 
    // by defering them to this work queue, this queue gets 
    // processed in Agent::KSync context
    KSyncRxWorkQueue  rx_process_queue_;
    static std::auto_ptr<KSyncSock> sock_;
    static pid_t pid_;
    static int vnsw_netlink_family_id_;
    // AgentSandeshContext used for KSync response handling
    // AgentSandeshContext used for decode is picked based on work-queue index
    // Picking AgentSandeshContext based on work-queue index also makes it
    // thread safe
    static AgentSandeshContext *agent_sandesh_ctx_[kRxWorkQueueCount];
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
    static void Init(boost::asio::io_service &ios, int protocol, bool use_work_queue,
                     const std::string &cpu_pin_policy);
private:
#ifdef _WIN32
    boost::asio::windows::stream_handle pipe_;
#else
    boost::asio::netlink::raw::socket sock_;
#endif
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

    static void Init(boost::asio::io_service &ios, int port,
                     const std::string &cpu_pin_policy);
private:
    boost::asio::ip::udp::socket sock_;
    boost::asio::ip::udp::endpoint server_ep_;
};

//Unix domain socket class for interacting with user vrouter
#define KSYNC_AGENT_VROUTER_SOCK_PATH "/var/run/vrouter/dpdk_netlink"
class KSyncSockUds : public KSyncSock {
public:
    KSyncSockUds(boost::asio::io_service &ios);
    virtual ~KSyncSockUds() {
        delete rx_buff_;
        delete rx_buff_q_;
    }

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
    virtual bool Run(void);

    static void Init(boost::asio::io_service &ios,
                     const std::string &cpu_pin_policy,
                     const std::string &sockpathvr="");
private:
#ifdef _WIN32
    //TODO: Win support?
#else
    boost::asio::local::stream_protocol::socket sock_;
    boost::asio::local::stream_protocol::endpoint server_ep_;
#endif
    char *rx_buff_;
    char *rx_buff_q_;
    size_t remain_;
    int socket_;
    int connected_;
    static string sockpath_;
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
    virtual ~KSyncSockTcpSession();
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
    virtual bool Run(void);

    bool ReceiveMsg(const u_int8_t *msg, size_t size);
    void OnSessionEvent(TcpSession *session, TcpSession::Event event);
    bool connect_complete() const {
        return connect_complete_;
    }
    void AsyncReadStart();

    static void Init(EventManager *evm,
                     boost::asio::ip::address ip_addr, int port,
                     const std::string &cpu_pin_policy);
private:
    EventManager *evm_;
    TcpSession *session_;
    boost::asio::ip::tcp::endpoint server_ep_;
    boost::asio::ip::tcp::socket *tcp_socket_;
    bool connect_complete_;
    char *rx_buff_;
    char *rx_buff_rem_;
    size_t remain_;
};
#endif // ctrlplane_ksync_sock_h
