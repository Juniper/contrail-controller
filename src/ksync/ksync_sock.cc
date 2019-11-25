/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include "base/os.h"
#if defined(__linux__)
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/sockios.h>
#elif defined(__FreeBSD__)
#include "vr_os.h"
#endif
#include <sys/socket.h>

#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include "ksync_index.h"
#include "ksync_entry.h"
#include "ksync_object.h"
#include "ksync_sock.h"
#include "ksync_sock_user.h"
#include "ksync_types.h"

#include "nl_util.h"
#include "udp_util.h"
#include "vr_genetlink.h"
#include "vr_types.h"

using namespace boost::asio;

/* Note SO_RCVBUFFORCE is supported only for linux version 2.6.14 and above */
typedef boost::asio::detail::socket_option::integer<SOL_SOCKET,
        SO_RCVBUFFORCE> ReceiveBuffForceSize;

int KSyncSock::vnsw_netlink_family_id_;
AgentSandeshContext *KSyncSock::agent_sandesh_ctx_[kRxWorkQueueCount];
std::auto_ptr<KSyncSock> KSyncSock::sock_;
pid_t KSyncSock::pid_;
tbb::atomic<bool> KSyncSock::shutdown_;

// Name of task used in KSync Response work-queues
const char* IoContext::io_wq_names[IoContext::MAX_WORK_QUEUES] =
                                                {
                                                    "Agent::Uve",
                                                    "Agent::KSync"
                                                };
/////////////////////////////////////////////////////////////////////////////
// Netlink utilities
/////////////////////////////////////////////////////////////////////////////
uint32_t GetNetlinkSeqno(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return nlh->nlmsg_seq;
}

bool NetlinkMsgDone(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return ((nlh->nlmsg_flags & NLM_F_MULTI) != 0);
}

// Common validation for netlink messages
bool ValidateNetlink(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    if (nlh->nlmsg_type == NLMSG_ERROR) {
        LOG(ERROR, "Netlink error for seqno " << nlh->nlmsg_seq << " len "
            << nlh->nlmsg_len);
        assert(0);
        return false;
    }

    if (nlh->nlmsg_len > KSyncSock::kBufLen) {
        LOG(ERROR, "Length of " << nlh->nlmsg_len << " is more than expected "
            "length of " << KSyncSock::kBufLen);
        assert(0);
        return false;
    }

    if (nlh->nlmsg_type == NLMSG_DONE) {
        return true;
    }

    // Sanity checks for generic-netlink message
    if (nlh->nlmsg_type != KSyncSock::GetNetlinkFamilyId()) {
        LOG(ERROR, "Netlink unknown message type : " << nlh->nlmsg_type);
        assert(0);
        return false;
    }

    struct genlmsghdr *genlh = (struct genlmsghdr *) (data + NLMSG_HDRLEN);
    if (genlh->cmd != SANDESH_REQUEST) {
        LOG(ERROR, "Unknown generic netlink cmd : " << genlh->cmd);
        assert(0);
        return false;
    }

    struct nlattr * attr = (struct nlattr *)(data + NLMSG_HDRLEN
                                             + GENL_HDRLEN);
    if (attr->nla_type != NL_ATTR_VR_MESSAGE_PROTOCOL) {
        LOG(ERROR, "Unknown generic netlink TLV type : " << attr->nla_type);
        assert(0);
        return false;
    }

    return true;
}

void GetNetlinkPayload(char *data, char **buf, uint32_t *buf_len) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    int len = 0;
    if (nlh->nlmsg_type == NLMSG_DONE) {
        len = NLMSG_HDRLEN;
    } else {
        len = NLMSG_HDRLEN + GENL_HDRLEN + NLA_HDRLEN;
    }

    *buf = data + len;
    *buf_len = nlh->nlmsg_len - len;
}

void InitNetlink(nl_client *client) {
    nl_init_generic_client_req(client, KSyncSock::GetNetlinkFamilyId());
    unsigned char *nl_buf;
    uint32_t nl_buf_len;
    assert(nl_build_header(client, &nl_buf, &nl_buf_len) >= 0);
}

void ResetNetlink(nl_client *client) {
    unsigned char *nl_buf;
    uint32_t nl_buf_len;
    client->cl_buf_offset = 0;
    nl_build_header(client, &nl_buf, &nl_buf_len);
}

void UpdateNetlink(nl_client *client, uint32_t len, uint32_t seq_no) {
    nl_update_header(client, len);
    struct nlmsghdr *nlh = (struct nlmsghdr *)client->cl_buf;
    nlh->nlmsg_pid = KSyncSock::GetPid();
    nlh->nlmsg_seq = seq_no;
}

void DecodeSandeshMessages(char *buf, uint32_t buf_len, SandeshContext *sandesh_context,
                           uint32_t alignment) {
    while (buf_len > (alignment - 1)) {
        int error;
        int decode_len = Sandesh::ReceiveBinaryMsgOne((uint8_t *)buf, buf_len,
                                                      &error, sandesh_context);
        if (decode_len < 0) {
            LOG(DEBUG, "Incorrect decode len " << decode_len);
            break;
        }
        buf += decode_len;
        buf_len -= decode_len;
    }
}

/////////////////////////////////////////////////////////////////////////////
// KSyncSock routines
/////////////////////////////////////////////////////////////////////////////
KSyncSock::KSyncSock() :
    nl_client_(NULL), wait_tree_(), send_queue_(this),
    max_bulk_msg_count_(kMaxBulkMsgCount), max_bulk_buf_size_(kMaxBulkMsgSize),
    bulk_seq_no_(kInvalidBulkSeqNo), bulk_buf_size_(0), bulk_msg_count_(0),
    rx_buff_(NULL), read_inline_(true), bulk_msg_context_(NULL),
    use_wait_tree_(true), process_data_inline_(false),
    ksync_bulk_sandesh_context_(), uve_bulk_sandesh_context_(),
    tx_count_(0), ack_count_(0), err_count_(0), 
    rx_process_queue_(TaskScheduler::GetInstance()->GetTaskId("Agent::KSync"), 0,
                    boost::bind(&KSyncSock::ProcessRxData, this, _1)) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    uint32_t uve_task_id =
        scheduler->GetTaskId(IoContext::io_wq_names[IoContext::IOC_UVE]);
    uint32_t ksync_task_id =
        scheduler->GetTaskId(IoContext::io_wq_names[IoContext::IOC_KSYNC]);
    for(uint32_t i = 0; i < kRxWorkQueueCount; i++) {
        ksync_rx_queue[i] = AllocQueue(ksync_bulk_sandesh_context_,
                                       ksync_task_id, i, "KSync Receive Queue");
        uve_rx_queue[i] = AllocQueue(uve_bulk_sandesh_context_,
                                     uve_task_id, i, "KSync UVE Receive Queue");
    }

    nl_client_ = (nl_client *)malloc(sizeof(nl_client));
    memset(nl_client_, 0, sizeof(nl_client));
    rx_buff_ = NULL;
    seqno_ = 0;
    uve_seqno_ = 0;

    memset(bulk_mctx_arr_, 0, sizeof(bulk_mctx_arr_));
    bmca_prod_ = bmca_cons_ = 0;
}

KSyncSock::~KSyncSock() {
    assert(wait_tree_.size() == 0);

    if (rx_buff_) {
        delete [] rx_buff_;
        rx_buff_ = NULL;
    }

    for(int i = 0; i < kRxWorkQueueCount; i++) {
        ksync_rx_queue[i]->Shutdown();
        delete ksync_rx_queue[i];

        uve_rx_queue[i]->Shutdown();
        delete uve_rx_queue[i];
    }

    if (nl_client_->cl_buf) {
        free(nl_client_->cl_buf);
    }
    free(nl_client_);
}

void KSyncSock::Shutdown() {
    shutdown_ = true;
    sock_->send_queue_.Shutdown();
    sock_.release();
}

void KSyncSock::Init(bool use_work_queue, const std::string &cpu_pin_policy) {
    sock_->send_queue_.Init(use_work_queue, cpu_pin_policy);
    pid_ = getpid();
    shutdown_ = false;
}

KSyncSock::KSyncReceiveQueue *KSyncSock::AllocQueue
(KSyncBulkSandeshContext ctxt[], uint32_t task_id, uint32_t instance,
 const char *name) {
    KSyncReceiveQueue *queue;
    queue = new KSyncReceiveQueue
        (task_id, instance, boost::bind(&KSyncSock::ProcessKernelData, this,
                                        &ctxt[instance], _1));
    char tmp[128];
    sprintf(tmp, "%s-%d", name, instance);
    queue->set_name(tmp);
    return queue;
}

void KSyncSock::SetMeasureQueueDelay(bool val) {
    sock_->send_queue_.set_measure_busy_time(val);
    for (int i = 0; i < kRxWorkQueueCount; i++) {
        ksync_rx_queue[i]->set_measure_busy_time(val);
    }
}

void KSyncSock::Start(bool read_inline) {
    sock_->read_inline_ = read_inline;
    if (sock_->read_inline_) {
        return;
    }
    sock_->rx_buff_ = new char[kBufLen];
    sock_->AsyncReceive(boost::asio::buffer(sock_->rx_buff_, kBufLen),
                        boost::bind(&KSyncSock::ReadHandler, sock_.get(),
                                    placeholders::error,
                                    placeholders::bytes_transferred));
}

void KSyncSock::SetSockTableEntry(KSyncSock *sock) {
    assert(sock_.get() == NULL);
    sock_.reset(sock);
}

void KSyncSock::SetNetlinkFamilyId(int id) {
    vnsw_netlink_family_id_ = id;
    InitNetlink(sock_->nl_client_);
}

uint32_t KSyncSock::WaitTreeSize() const {
    return wait_tree_.size();
}

void KSyncSock::SetSeqno(uint32_t seq) {
    seqno_ = seq;
    uve_seqno_ = seq;
}

uint32_t KSyncSock::AllocSeqNo(IoContext::Type type, uint32_t instance) {
    uint32_t seq;
    if (type == IoContext::IOC_UVE) {
        seq = uve_seqno_.fetch_and_add(1);
        seq = (seq * kRxWorkQueueCount + (instance % kRxWorkQueueCount)) << 1;
    } else {
        seq = seqno_.fetch_and_add(1);
        seq = (seq * kRxWorkQueueCount + (instance % kRxWorkQueueCount)) << 1;
        seq |= KSYNC_DEFAULT_Q_ID_SEQ;
    }
    if (seq == kInvalidBulkSeqNo) {
        return AllocSeqNo(type, instance);
    }
    return seq;
}

uint32_t KSyncSock::AllocSeqNo(IoContext::Type type) {
    return AllocSeqNo(type, 0);
}

KSyncSock::KSyncReceiveQueue *KSyncSock::GetReceiveQueue(IoContext::Type type,
                                                         uint32_t instance) {
    if (type == IoContext::IOC_UVE) {
        return uve_rx_queue[instance % kRxWorkQueueCount];
    } else {
        return ksync_rx_queue[instance % kRxWorkQueueCount];
    }
}

KSyncSock::KSyncReceiveQueue *KSyncSock::GetReceiveQueue(uint32_t seqno) {
    IoContext::Type type;
    if (seqno & KSYNC_DEFAULT_Q_ID_SEQ)
        type = IoContext::IOC_KSYNC;
    else
        type = IoContext::IOC_UVE;

    uint32_t instance = (seqno >> 1) % kRxWorkQueueCount;
    return GetReceiveQueue(type, instance);
}
void KSyncSock::EnqueueRxProcessData(KSyncEntry *entry, KSyncEntry::KSyncEvent event) {
    rx_process_queue_.Enqueue(KSyncRxQueueData(entry, event));
}
bool KSyncSock::ProcessRxData(KSyncRxQueueData data) {
    assert(data.event_ != KSyncEntry::INVALID);
    KSyncObject *object = data.entry_->GetObject();
    object->NetlinkAck(data.entry_, data.event_);
    return true;
}
KSyncBulkSandeshContext *KSyncSock::GetBulkSandeshContext(uint32_t seqno) {

    uint32_t instance = (seqno >> 1) % kRxWorkQueueCount;
    if (seqno & KSYNC_DEFAULT_Q_ID_SEQ)
        return &ksync_bulk_sandesh_context_[instance];
    else
        return &uve_bulk_sandesh_context_[instance];
}

KSyncSock *KSyncSock::Get(DBTablePartBase *partition) {
    return sock_.get();
}

KSyncSock *KSyncSock::Get(int idx) {
    assert(idx == 0);
    return sock_.get();
}

bool KSyncSock::ValidateAndEnqueue(char *data, KSyncBulkMsgContext *context) {
    Validate(data);

    KSyncReceiveQueue *queue;
    if (context) {
        queue = GetReceiveQueue(context->io_context_type(),
                                context->work_queue_index());
    } else {
        queue = GetReceiveQueue(GetSeqno(data));
    }
    queue->Enqueue(KSyncRxData(data, context));
    return true;
}

// Read handler registered with boost::asio. Demux done based on seqno_
void KSyncSock::ReadHandler(const boost::system::error_code& error,
                            size_t bytes_transferred) {
    if (error) {
        LOG(ERROR, "Error reading from Ksync sock. Error : " <<
            boost::system::system_error(error).what());
        if (shutdown_ == false) {
            assert(0);
        }
        return;
    }

    ValidateAndEnqueue(rx_buff_, NULL);

    rx_buff_ = new char[kBufLen];
    AsyncReceive(boost::asio::buffer(rx_buff_, kBufLen),
                 boost::bind(&KSyncSock::ReadHandler, this,
                             placeholders::error,
                             placeholders::bytes_transferred));
}

// Process kernel data - executes in the task specified by IoContext
// Currently only Agent::KSync and Agent::Uve are possibilities
bool KSyncSock::ProcessKernelData(KSyncBulkSandeshContext *bulk_sandesh_context,
                                  const KSyncRxData &data) {
    KSyncBulkMsgContext *bulk_message_context = data.bulk_msg_context_;
    WaitTree::iterator it;
    if (data.bulk_msg_context_ == NULL) {
        uint32_t seqno = GetSeqno(data.buff_);
        {
            tbb::mutex::scoped_lock lock(mutex_);
            it = wait_tree_.find(seqno);
        }
        if (it == wait_tree_.end()) {
            LOG(ERROR, "KSync error in finding for sequence number : "
                << seqno);
            assert(0);
        }
        bulk_message_context = &(it->second);
    }

    bulk_sandesh_context->set_bulk_message_context(bulk_message_context);
    BulkDecoder(data.buff_, bulk_sandesh_context);
    // Remove the IoContext only on last netlink message
    if (IsMoreData(data.buff_) == false) {
        if (data.bulk_msg_context_ != NULL) {
            delete data.bulk_msg_context_;
        } else {
            tbb::mutex::scoped_lock lock(mutex_);
            wait_tree_.erase(it);
        }
    }
    delete[] data.buff_;
    return true;
}

bool KSyncSock::BlockingRecv() {
    char data[kBufLen];
    bool ret = false;

    do {
        Receive(boost::asio::buffer(data, kBufLen));
        AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext(0);
        ctxt->SetErrno(0);
        // BlockingRecv used only during Init and doesnt support bulk messages
        // Use non-bulk version of decoder
        Decoder(data, ctxt);
        if (ctxt->GetErrno() != 0 && ctxt->GetErrno() != EEXIST) {
            KSYNC_ERROR(VRouterError, "VRouter operation failed. Error <",
                        ctxt->GetErrno(), ":",
                        KSyncEntry::VrouterErrorToString(ctxt->GetErrno()),
                        ">. Object <", "N/A", ">. State <", "N/A",
                        ">. Message number :", 0);
            ret = true;
        }
    } while (IsMoreData(data));

    return ret;
}

// BlockingSend does not support bulk messages.
size_t KSyncSock::BlockingSend(char *msg, int msg_len) {
    KSyncBufferList iovec;
    iovec.push_back(buffer(msg, msg_len));
    bulk_buf_size_ = msg_len;
    return SendTo(&iovec, 0);
}

void KSyncSock::GenericSend(IoContext *ioc) {
    send_queue_.Enqueue(ioc);
}

void KSyncSock::SendAsync(KSyncEntry *entry, int msg_len, char *msg,
                          KSyncEntry::KSyncEvent event) {
    KSyncIoContext *ioc = new KSyncIoContext(this, entry, msg_len, msg, event);
    // Pre-allocate buffers to minimize processing in KSyncTxQueue context
    if (read_inline_ && entry->pre_alloc_rx_buffer()) {
        ioc->rx_buffer1_ = new char [kBufLen];
        ioc->rx_buffer2_ = new char [kBufLen];
    } else {
        ioc->rx_buffer1_ = ioc->rx_buffer2_ = NULL;
    }
    send_queue_.Enqueue(ioc);
}

// Write handler registered with boost::asio
void KSyncSock::WriteHandler(const boost::system::error_code& error,
                             size_t bytes_transferred) {
    if (error) {
        LOG(ERROR, "Ksync sock write error : " <<
            boost::system::system_error(error).what());
        if (shutdown_ == false) {
            assert(0);
        }
    }
}

// End of messages in the work-queue. Send messages pending in bulk context
void KSyncSock::OnEmptyQueue(bool done) {
    if (bulk_seq_no_ == kInvalidBulkSeqNo)
        return;

    KSyncBulkMsgContext *bulk_message_context = NULL;
    if (use_wait_tree_) {
        if (read_inline_ == false) {
            tbb::mutex::scoped_lock lock(mutex_);
            WaitTree::iterator it = wait_tree_.find(bulk_seq_no_);
            assert(it != wait_tree_.end());
            bulk_message_context = &it->second;
        } else {
            bulk_message_context = bulk_msg_context_;
        }
    } else {
        bulk_message_context = bulk_mctx_arr_[bmca_prod_];
    }

    SendBulkMessage(bulk_message_context, bulk_seq_no_);
}

// Send messages accumilated in bulk context
int KSyncSock::SendBulkMessage(KSyncBulkMsgContext *bulk_message_context,
                               uint32_t seqno) {
    KSyncBufferList iovec;
    // Get all buffers to send into single io-vector
    bulk_message_context->Data(&iovec);
    tx_count_++;

    if (!read_inline_) {
        if (!use_wait_tree_) {
            bmca_prod_++;
            if (bmca_prod_ >= KSYNC_BMC_ARR_SIZE) {
                bmca_prod_ = 0;
            }
        }

        AsyncSendTo(&iovec, seqno,
                    boost::bind(&KSyncSock::WriteHandler, this,
                                placeholders::error,
                                placeholders::bytes_transferred));
    } else {
        SendTo(&iovec, seqno);
        bool more_data = false;
        do {
            char *rxbuf = bulk_message_context->GetReceiveBuffer();
            Receive(boost::asio::buffer(rxbuf, kBufLen));
            more_data = IsMoreData(rxbuf);
            if (!process_data_inline_) {
                ValidateAndEnqueue(rxbuf, bulk_message_context);
            } else {
                ProcessDataInline(rxbuf);
            }
        } while(more_data);
    }

    bulk_msg_context_ = NULL;
    bulk_seq_no_ = kInvalidBulkSeqNo;
    return true;
}

// Get the bulk-context for sequence-number
KSyncBulkMsgContext *KSyncSock::LocateBulkContext
(uint32_t seqno, IoContext::Type io_context_type,
 uint32_t work_queue_index) {
    if (read_inline_) {
        if (bulk_seq_no_ == kInvalidBulkSeqNo) {
            assert(bulk_msg_context_ == NULL);
            bulk_seq_no_ = seqno;
            bulk_buf_size_ = 0;
            bulk_msg_count_ = 0;
            bulk_msg_context_ = new KSyncBulkMsgContext(io_context_type,
                                                        work_queue_index);
        }
        return bulk_msg_context_;
    }

    if (use_wait_tree_) {
        tbb::mutex::scoped_lock lock(mutex_);
        if (bulk_seq_no_ == kInvalidBulkSeqNo) {
            bulk_seq_no_ = seqno;
            bulk_buf_size_ = 0;
            bulk_msg_count_ = 0;

            wait_tree_.insert(WaitTreePair(seqno,
                                       KSyncBulkMsgContext(io_context_type,
                                                           work_queue_index)));
        }

        WaitTree::iterator it = wait_tree_.find(bulk_seq_no_);
        assert(it != wait_tree_.end());
        return &it->second;
    } else {
        if (bulk_seq_no_ == kInvalidBulkSeqNo) {
            bulk_seq_no_ = seqno;
            bulk_buf_size_ = 0;
            bulk_msg_count_ = 0;

            bulk_mctx_arr_[bmca_prod_] = new KSyncBulkMsgContext(io_context_type,
                                                            work_queue_index);
            bulk_mctx_arr_[bmca_prod_]->set_seqno(seqno);
        }

        return bulk_mctx_arr_[bmca_prod_];
    }

    return NULL;
}

// Try adding an io-context to bulk context. Returns
//  - true  : if message can be added to bulk context
//  - false : if message cannot be added to bulk context
bool KSyncSock::TryAddToBulk(KSyncBulkMsgContext *bulk_message_context,
                             IoContext *ioc) {
    if ((bulk_buf_size_ + ioc->GetMsgLen()) >= max_bulk_buf_size_)
        return false;

    if (bulk_msg_count_ >= max_bulk_msg_count_)
        return false;

    if (bulk_message_context->io_context_type() != ioc->type())
        return false;

    if (use_wait_tree_) {
        if (bulk_message_context->work_queue_index() != ioc->index())
            return false;
    }

    bulk_buf_size_ += ioc->GetMsgLen();
    bulk_msg_count_++;

    bulk_message_context->Insert(ioc);
    if (ioc->rx_buffer1()) {
        bulk_message_context->AddReceiveBuffer(ioc->rx_buffer1());
        ioc->reset_rx_buffer1();

    }
    if (ioc->rx_buffer2()) {
        bulk_message_context->AddReceiveBuffer(ioc->rx_buffer2());
        ioc->reset_rx_buffer2();
    }
    return true;
}

bool KSyncSock::SendAsyncImpl(IoContext *ioc) {
    KSyncBulkMsgContext *bulk_message_context =
        LocateBulkContext(ioc->GetSeqno(), ioc->type(), ioc->index());
    // Try adding message to bulk-message list
    if (TryAddToBulk(bulk_message_context, ioc)) {
        // Message added to bulk-list. Nothing more to do
        return true;
    }

    // Message cannot be added to bulk-list. Send the current list
    SendBulkMessage(bulk_message_context, bulk_seq_no_);

    // Allocate a new context and add message to it
    bulk_message_context = LocateBulkContext(ioc->GetSeqno(), ioc->type(),
                                             ioc->index());
    assert(TryAddToBulk(bulk_message_context, ioc));
    return true;
}


/////////////////////////////////////////////////////////////////////////////
// KSyncSockNetlink routines
/////////////////////////////////////////////////////////////////////////////
KSyncSockNetlink::KSyncSockNetlink(boost::asio::io_service &ios, int protocol)
    : sock_(ios, protocol) {
    ReceiveBuffForceSize set_rcv_buf;
    set_rcv_buf = KSYNC_SOCK_RECV_BUFF_SIZE;
    boost::system::error_code ec;
    sock_.set_option(set_rcv_buf, ec);
    if (ec.value() != 0) {
        LOG(ERROR, "Error Changing netlink receive sock buffer size to " <<
                set_rcv_buf.value() << " error = " <<
                boost::system::system_error(ec).what());
    }
    boost::asio::socket_base::receive_buffer_size rcv_buf_size;
    boost::system::error_code ec1;
    sock_.get_option(rcv_buf_size, ec);
    LOG(INFO, "Current receive sock buffer size is " << rcv_buf_size.value());
}

KSyncSockNetlink::~KSyncSockNetlink() {
}

void KSyncSockNetlink::Init(io_service &ios, int protocol, bool use_work_queue,
                            const std::string &cpu_pin_policy) {
    KSyncSock::SetSockTableEntry(new KSyncSockNetlink(ios, protocol));
    KSyncSock::Init(use_work_queue, cpu_pin_policy);
}

uint32_t KSyncSockNetlink::GetSeqno(char *data) {
    return GetNetlinkSeqno(data);
}

bool KSyncSockNetlink::IsMoreData(char *data) {
    return NetlinkMsgDone(data);
}

bool KSyncSockNetlink::Validate(char *data) {
    return ValidateNetlink(data);
}

//netlink socket class for interacting with kernel
void KSyncSockNetlink::AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                                   HandlerCb cb) {
    ResetNetlink(nl_client_);
    KSyncBufferList::iterator it = iovec->begin();
    iovec->insert(it, buffer((char *)nl_client_->cl_buf,
                             nl_client_->cl_buf_offset));
    UpdateNetlink(nl_client_, bulk_buf_size_, seq_no);

    boost::asio::netlink::raw::endpoint ep;
    sock_.async_send_to(*iovec, ep, cb);
}

size_t KSyncSockNetlink::SendTo(KSyncBufferList *iovec, uint32_t seq_no) {
    ResetNetlink(nl_client_);
    KSyncBufferList::iterator it = iovec->begin();
    iovec->insert(it, buffer((char *)nl_client_->cl_buf,
                             nl_client_->cl_buf_offset));
    UpdateNetlink(nl_client_, bulk_buf_size_, seq_no);

    boost::asio::netlink::raw::endpoint ep;
    return sock_.send_to(*iovec, ep);
}

// Static method to decode non-bulk message
void KSyncSockNetlink::NetlinkDecoder(char *data, SandeshContext *ctxt) {
    assert(ValidateNetlink(data));
    char *buf = NULL;
    uint32_t buf_len = 0;
    GetNetlinkPayload(data, &buf, &buf_len);
    DecodeSandeshMessages(buf, buf_len, ctxt, NLA_ALIGNTO);
}

bool KSyncSockNetlink::Decoder(char *data, AgentSandeshContext *context) {
    NetlinkDecoder(data, context);
    return true;
}

// Static method used in ksync_sock_user only
void KSyncSockNetlink::NetlinkBulkDecoder(char *data, SandeshContext *ctxt,
                                          bool more) {
    assert(ValidateNetlink(data));
    char *buf = NULL;
    uint32_t buf_len = 0;
    GetNetlinkPayload(data, &buf, &buf_len);
    KSyncBulkSandeshContext *bulk_sandesh_context =
        dynamic_cast<KSyncBulkSandeshContext *>(ctxt);
    bulk_sandesh_context->Decoder(buf, buf_len, NLA_ALIGNTO, more);
}

bool KSyncSockNetlink::BulkDecoder(char *data,
                                   KSyncBulkSandeshContext *bulk_sandesh_context) {
    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetNetlinkPayload(data, &buf, &buf_len);
    return bulk_sandesh_context->Decoder(buf, buf_len, NLA_ALIGNTO, IsMoreData(data));
}

void KSyncSockNetlink::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    sock_.async_receive(buf, cb);
}

void KSyncSockNetlink::Receive(mutable_buffers_1 buf) {
    sock_.receive(buf);
    struct nlmsghdr *nlh = buffer_cast<struct nlmsghdr *>(buf);
    if (nlh->nlmsg_type == NLMSG_ERROR) {
        LOG(ERROR, "Netlink error for seqno " << nlh->nlmsg_seq
                << " len " << nlh->nlmsg_len);
        assert(0);
    }
}

/////////////////////////////////////////////////////////////////////////////
// KSyncSockUdp routines
/////////////////////////////////////////////////////////////////////////////
//Udp socket class for interacting with kernel
KSyncSockUdp::KSyncSockUdp(boost::asio::io_service &ios, int port) :
    sock_(ios, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0)),
    server_ep_(boost::asio::ip::address::from_string("127.0.0.1"), port) {
}

void KSyncSockUdp::Init(io_service &ios, int port,
                        const std::string &cpu_pin_policy) {
    KSyncSock::SetSockTableEntry(new KSyncSockUdp(ios, port));
    KSyncSock::Init(false, cpu_pin_policy);
}

uint32_t KSyncSockUdp::GetSeqno(char *data) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    return hdr->seq_no;
}

bool KSyncSockUdp::IsMoreData(char *data) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    return ((hdr->flags & UVR_MORE) == UVR_MORE);
}

// We dont expect any non-bulk operation on UDP
bool KSyncSockUdp::Decoder(char *data, AgentSandeshContext *context) {
    assert(0);
    return false;
}

bool KSyncSockUdp::BulkDecoder(char *data,
                               KSyncBulkSandeshContext *bulk_sandesh_context) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    uint32_t buf_len = hdr->msg_len;
    char *buf = data + sizeof(struct uvr_msg_hdr);
    return bulk_sandesh_context->Decoder(buf, buf_len, 1, IsMoreData(data));
}

void KSyncSockUdp::AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                               HandlerCb cb) {
    struct uvr_msg_hdr hdr;
    hdr.seq_no = seq_no;
    hdr.flags = 0;
    hdr.msg_len = bulk_buf_size_;

    KSyncBufferList::iterator it = iovec->begin();
    iovec->insert(it, buffer((char *)(&hdr), sizeof(hdr)));

    sock_.async_send_to(*iovec, server_ep_, cb);
}

size_t KSyncSockUdp::SendTo(KSyncBufferList *iovec, uint32_t seq_no) {
    struct uvr_msg_hdr hdr;
    hdr.seq_no = seq_no;
    hdr.flags = 0;
    hdr.msg_len = bulk_buf_size_;

    KSyncBufferList::iterator it = iovec->begin();
    iovec->insert(it, buffer((char *)(&hdr), sizeof(hdr)));

    return sock_.send_to(*iovec, server_ep_, MSG_DONTWAIT);
}

bool KSyncSockUdp::Validate(char *data) {
    return true;
}

void KSyncSockUdp::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    boost::asio::ip::udp::endpoint ep;
    sock_.async_receive_from(buf, ep, cb);
}

void KSyncSockUdp::Receive(mutable_buffers_1 buf) {
    boost::asio::ip::udp::endpoint ep;
    sock_.receive_from(buf, ep);
}

void KSyncSock::ProcessDataInline(char *data) {
    KSyncBulkMsgContext *bulk_message_context = NULL;
    KSyncBulkSandeshContext *bulk_sandesh_context;
    uint32_t seqno = GetSeqno(data);

    assert(!use_wait_tree_);
    Validate(data);

    bulk_sandesh_context = GetBulkSandeshContext(seqno);
    bulk_message_context = bulk_mctx_arr_[bmca_cons_];
    assert(bulk_message_context->seqno() == seqno);

    bulk_sandesh_context->set_bulk_message_context(bulk_message_context);
    BulkDecoder(data, bulk_sandesh_context);

    // Remove the IoContext only on last netlink message
    if (IsMoreData(data) == false) {
        delete bulk_message_context;
        bmca_cons_++;
        if (bmca_cons_ >= KSYNC_BMC_ARR_SIZE) {
            bmca_cons_ = 0;
        }
    }

    return;
}

/////////////////////////////////////////////////////////////////////////////
// KSyncIoContext routines
/////////////////////////////////////////////////////////////////////////////
KSyncIoContext::KSyncIoContext(KSyncSock *sock, KSyncEntry *sync_entry,
                               int msg_len, char *msg,
                               KSyncEntry::KSyncEvent event) :
    IoContext(msg, msg_len, 0,
              sock->GetAgentSandeshContext(sync_entry->GetTableIndex()),
              IoContext::IOC_KSYNC, sync_entry->GetTableIndex()),
    entry_(sync_entry), event_(event), sock_(sock) {
    SetSeqno(sock->AllocSeqNo(type(), index()));
}

void KSyncIoContext::Handler() {
    sock_->EnqueueRxProcessData(entry_, event_);
}

void KSyncIoContext::ErrorHandler(int err) {
    entry_->ErrorHandler(err, GetSeqno(), event_);
}

/////////////////////////////////////////////////////////////////////////////
// Routines for KSyncBulkSandeshContext
/////////////////////////////////////////////////////////////////////////////
KSyncBulkSandeshContext::KSyncBulkSandeshContext() :
    AgentSandeshContext(), bulk_msg_context_(NULL)  { }

KSyncBulkSandeshContext::~KSyncBulkSandeshContext() {
}

// Sandesh responses for old context are done. Check for any errors
void KSyncBulkSandeshContext::IoContextDone() {
    IoContext *io_context = &(*bulk_msg_context_->io_context_list_it_);
    AgentSandeshContext *sandesh_context = io_context->GetSandeshContext();

    sandesh_context->set_ksync_io_ctx(NULL);
    if (sandesh_context->GetErrno() != 0 &&
        sandesh_context->GetErrno() != EEXIST) {
        io_context->ErrorHandler(sandesh_context->GetErrno());
    }
    io_context->Handler();
}

void KSyncBulkSandeshContext::IoContextStart() {
    bulk_msg_context_->vr_response_count_++;
    IoContext &io_context = *bulk_msg_context_->io_context_list_it_;
    AgentSandeshContext *sandesh_context = io_context.GetSandeshContext();
    sandesh_context->set_ksync_io_ctx
        (static_cast<KSyncIoContext *>(&io_context));
}

// Process the sandesh messages
// There can be more then one sandesh messages in the netlink buffer.
// Iterate and process all of them
bool KSyncBulkSandeshContext::Decoder(char *data, uint32_t len,
                                      uint32_t alignment, bool more) {
    DecodeSandeshMessages(data, len, this, alignment);
    assert(bulk_msg_context_->io_context_list_it_ !=
           bulk_msg_context_->io_context_list_.end());
    if (more == true)
        return false;

    IoContextDone();

    // No more netlink messages. Validate that iterator points to last element
    // in IoContextList
    bulk_msg_context_->io_context_list_it_++;
    assert(bulk_msg_context_->io_context_list_it_ ==
           bulk_msg_context_->io_context_list_.end());
    return true;
}

void KSyncBulkSandeshContext::SetErrno(int err) {
    AgentSandeshContext *context = GetSandeshContext();
    context->SetErrno(err);
}

AgentSandeshContext *KSyncBulkSandeshContext::GetSandeshContext() {
    assert(bulk_msg_context_->vr_response_count_);
    return bulk_msg_context_->io_context_list_it_->GetSandeshContext();
}

void KSyncBulkSandeshContext::IfMsgHandler(vr_interface_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->IfMsgHandler(req);
}

void KSyncBulkSandeshContext::NHMsgHandler(vr_nexthop_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->NHMsgHandler(req);
}

void KSyncBulkSandeshContext::RouteMsgHandler(vr_route_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->RouteMsgHandler(req);
}

void KSyncBulkSandeshContext::MplsMsgHandler(vr_mpls_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->MplsMsgHandler(req);
}

void KSyncBulkSandeshContext::QosConfigMsgHandler(vr_qos_map_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->QosConfigMsgHandler(req);
}

void KSyncBulkSandeshContext::ForwardingClassMsgHandler(vr_fc_map_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->ForwardingClassMsgHandler(req);
}

// vr_response message is treated as delimiter in a bulk-context. So, move to
// next io-context within bulk-message context.
int KSyncBulkSandeshContext::VrResponseMsgHandler(vr_response *resp) {
    AgentSandeshContext *sandesh_context = NULL;
    // If this is first vr_reponse received, move io-context to first entry in
    // bulk context
    if (bulk_msg_context_->vr_response_count_ == 0) {
        bulk_msg_context_->io_context_list_it_ =
            bulk_msg_context_->io_context_list_.begin();
        sandesh_context =
            bulk_msg_context_->io_context_list_it_->GetSandeshContext();
        IoContextStart();
    } else {
        // Sandesh responses for old io-context are done.
        // Check for any errors and trigger state-machine for old io-context
        IoContextDone();
        // Move to the next io-context
        bulk_msg_context_->io_context_list_it_++;
        assert(bulk_msg_context_->io_context_list_it_ !=
               bulk_msg_context_->io_context_list_.end());
        sandesh_context =
            bulk_msg_context_->io_context_list_it_->GetSandeshContext();
        IoContextStart();
    }
    return sandesh_context->VrResponseMsgHandler(resp);
}

void KSyncBulkSandeshContext::MirrorMsgHandler(vr_mirror_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->MirrorMsgHandler(req);
}

void KSyncBulkSandeshContext::FlowMsgHandler(vr_flow_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->FlowMsgHandler(req);
}

void KSyncBulkSandeshContext::FlowResponseHandler(vr_flow_response *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->FlowResponseHandler(req);
}

void KSyncBulkSandeshContext::VrfAssignMsgHandler(vr_vrf_assign_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->VrfAssignMsgHandler(req);
}

void KSyncBulkSandeshContext::VrfMsgHandler(vr_vrf_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->VrfMsgHandler(req);
}

void KSyncBulkSandeshContext::VrfStatsMsgHandler(vr_vrf_stats_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->VrfStatsMsgHandler(req);
}

void KSyncBulkSandeshContext::DropStatsMsgHandler(vr_drop_stats_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->DropStatsMsgHandler(req);
}

void KSyncBulkSandeshContext::VxLanMsgHandler(vr_vxlan_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->VxLanMsgHandler(req);
}

void KSyncBulkSandeshContext::VrouterOpsMsgHandler(vrouter_ops *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->VrouterOpsMsgHandler(req);
}

/////////////////////////////////////////////////////////////////////////////
// KSyncBulkMsgContext routines
/////////////////////////////////////////////////////////////////////////////
KSyncBulkMsgContext::KSyncBulkMsgContext(IoContext::Type type,
                                         uint32_t index) :
    io_context_list_(), io_context_type_(type), work_queue_index_(index),
    rx_buffer_index_(0), vr_response_count_(0), io_context_list_it_() {
}

KSyncBulkMsgContext::KSyncBulkMsgContext(const KSyncBulkMsgContext &rhs) :
    io_context_list_(), io_context_type_(rhs.io_context_type_),
    work_queue_index_(rhs.work_queue_index_),
    rx_buffer_index_(0), vr_response_count_(0), io_context_list_it_() {
    assert(rhs.vr_response_count_ == 0);
    assert(rhs.rx_buffer_index_ == 0);
    assert(rhs.io_context_list_.size() == 0);
}

struct IoContextDisposer {
    void operator() (IoContext *io_context) { delete io_context; }
};

KSyncBulkMsgContext::~KSyncBulkMsgContext() {
    assert(vr_response_count_ == io_context_list_.size());
    io_context_list_.clear_and_dispose(IoContextDisposer());
    for (uint32_t i = 0; i < rx_buffer_index_; i++) {
        delete[] rx_buffers_[i];
    }
}

char *KSyncBulkMsgContext::GetReceiveBuffer() {
    if (rx_buffer_index_ == 0)
        return new char[KSyncSock::kBufLen];

    return rx_buffers_[--rx_buffer_index_];
}

void KSyncBulkMsgContext::AddReceiveBuffer(char *buff) {
    assert(rx_buffer_index_ < kMaxRxBufferCount);
    rx_buffers_[rx_buffer_index_++] = buff;
}

void KSyncBulkMsgContext::Insert(IoContext *ioc) {
    io_context_list_.push_back(*ioc);
    return;
}

void KSyncBulkMsgContext::Data(KSyncBufferList *iovec) {
    IoContextList::iterator it = io_context_list_.begin();
    while (it != io_context_list_.end()) {
        iovec->push_back(buffer(it->GetMsg(), it->GetMsgLen()));
        it++;
    }
}
