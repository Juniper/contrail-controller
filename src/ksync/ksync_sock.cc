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

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

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
AgentSandeshContext *KSyncSock::agent_sandesh_ctx_;
std::auto_ptr<KSyncSock> KSyncSock::sock_;
pid_t KSyncSock::pid_;
tbb::atomic<bool> KSyncSock::shutdown_;

const char* IoContext::io_wq_names[IoContext::MAX_WORK_QUEUES] = 
                                                {"Agent::KSync", "Agent::Uve"};

static uint32_t IoVectorLength(KSyncBufferList *iovec) {
    KSyncBufferList::iterator it = iovec->begin();
    int offset = 0;
    while (it != iovec->end()) {
        offset +=  boost::asio::buffer_size(*it);
        it++;
    }
    return offset;
}

// Copy data from io-vector to a buffer
static uint32_t IoVectorToData(char *data, KSyncBufferList *iovec) {
    KSyncBufferList::iterator it = iovec->begin();
    int offset = 0;
    while (it != iovec->end()) {
        unsigned char *buf = boost::asio::buffer_cast<unsigned char *>(*it);
        memcpy(data + offset, buf, boost::asio::buffer_size(*it));
        offset +=  boost::asio::buffer_size(*it);
        it++;
    }
    return offset;
}
/////////////////////////////////////////////////////////////////////////////
// Netlink utilities
/////////////////////////////////////////////////////////////////////////////
static uint32_t GetNetlinkSeqno(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return nlh->nlmsg_seq;
}

static bool NetlinkMsgDone(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return ((nlh->nlmsg_flags & NLM_F_MULTI) != 0);
}

// Common validation for netlink messages
static bool ValidateNetlink(char *data) {
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

static void GetNetlinkPayload(char *data, char **buf, uint32_t *buf_len) {
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

static void InitNetlink(nl_client *client) {
    nl_init_generic_client_req(client, KSyncSock::GetNetlinkFamilyId());
    unsigned char *nl_buf;
    uint32_t nl_buf_len;
    assert(nl_build_header(client, &nl_buf, &nl_buf_len) >= 0);
}

static void ResetNetlink(nl_client *client) {
    unsigned char *nl_buf;
    uint32_t nl_buf_len;
    client->cl_buf_offset = 0;
    nl_build_header(client, &nl_buf, &nl_buf_len);
}

static void UpdateNetlink(nl_client *client, uint32_t len, uint32_t seq_no) {
    nl_update_header(client, len);
    struct nlmsghdr *nlh = (struct nlmsghdr *)client->cl_buf;
    nlh->nlmsg_pid = KSyncSock::GetPid();
    nlh->nlmsg_seq = seq_no;
}

static void DecodeSandeshMessages(char *buf, uint32_t buf_len,
                                  SandeshContext *sandesh_context,
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
    send_queue_(this),
    max_bulk_msg_count_(kMaxBulkMsgCount), max_bulk_buf_size_(kMaxBulkMsgSize),
    bulk_seq_no_(-1), tx_count_(0), err_count_(0), read_inline_(true) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    uint32_t task_id = 0;
    for(int i = 0; i < IoContext::MAX_WORK_QUEUES; i++) {
        task_id = scheduler->GetTaskId(IoContext::io_wq_names[i]);
        receive_work_queue[i] =
            new WorkQueue<char *>(task_id, 0,
                                  boost::bind(&KSyncSock::ProcessKernelData,
                                              this, _1));
    }
    task_id = scheduler->GetTaskId("Ksync::AsyncSend");
    nl_client_ = (nl_client *)malloc(sizeof(nl_client));
    bzero(nl_client_, sizeof(nl_client));
    rx_buff_ = NULL;
    seqno_ = 0;
    uve_seqno_ = 0;
}

KSyncSock::~KSyncSock() {
    assert(wait_tree_.size() == 0);

    if (rx_buff_) {
        delete [] rx_buff_;
        rx_buff_ = NULL;
    }

    for(int i = 0; i < IoContext::MAX_WORK_QUEUES; i++) {
        receive_work_queue[i]->Shutdown();
        delete receive_work_queue[i];
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

void KSyncSock::Init(bool use_work_queue) {
    sock_->send_queue_.Init(use_work_queue);
    pid_ = getpid();
    shutdown_ = false;
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

int KSyncSock::AllocSeqNo(bool is_uve) { 
    int seq;
    if (is_uve) {
        seq = uve_seqno_.fetch_and_add(2);
    } else {
        seq = seqno_.fetch_and_add(2);
        seq |= KSYNC_DEFAULT_Q_ID_SEQ;
    }
    return seq;
}

KSyncSock *KSyncSock::Get(DBTablePartBase *partition) {
    return sock_.get();
}

KSyncSock *KSyncSock::Get(int idx) {
    assert(idx == 0);
    return sock_.get();
}

bool KSyncSock::ValidateAndEnqueue(char *data) {
    Validate(data);
    IoContext::IoContextWorkQId q_id;
    if ((GetSeqno(data) & KSYNC_DEFAULT_Q_ID_SEQ) ==
        KSYNC_DEFAULT_Q_ID_SEQ) {
        q_id = IoContext::DEFAULT_Q_ID;
    } else {
        q_id = IoContext::UVE_Q_ID;
    }
    receive_work_queue[q_id]->Enqueue(data);
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

    ValidateAndEnqueue(rx_buff_);

    rx_buff_ = new char[kBufLen];
    AsyncReceive(boost::asio::buffer(rx_buff_, kBufLen),
                 boost::bind(&KSyncSock::ReadHandler, this,
                             placeholders::error,
                             placeholders::bytes_transferred));
}

// Process kernel data - executes in the task specified by IoContext
// Currently only Agent::KSync and Agent::Uve are possibilities
bool KSyncSock::ProcessKernelData(char *data) {
    uint32_t seqno = GetSeqno(data);
    WaitTree::iterator it;
    {
        tbb::mutex::scoped_lock lock(mutex_);
        it = wait_tree_.find(seqno);
    }
    if (it == wait_tree_.end()) {
        LOG(ERROR, "KSync error in finding for sequence number : " << seqno);
        assert(0);
    }
    KSyncBulkSandeshContext *bulk_context = &(it->second);

    BulkDecoder(data, bulk_context);
    // Remove the IoContext only on last netlink message
    if (IsMoreData(data) == false) {
        tbb::mutex::scoped_lock lock(mutex_);
        wait_tree_.erase(it);
    }
    delete[] data;
    return true;
}

bool KSyncSock::BlockingRecv() {
    char data[kBufLen];
    bool ret = false;

    do {
        Receive(boost::asio::buffer(data, kBufLen));
        AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext();
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
    uint32_t seq = AllocSeqNo(false);
    KSyncIoContext *ioc = new KSyncIoContext(entry, msg_len, msg, seq, event);
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
    if (bulk_seq_no_ == -1)
        return;
    tbb::mutex::scoped_lock lock(mutex_);
    WaitTree::iterator it = wait_tree_.find(bulk_seq_no_);
    assert(it != wait_tree_.end());
    KSyncBulkSandeshContext *bulk_context = &it->second;
    SendBulkMessage(bulk_context, bulk_seq_no_);
}

// Send messages accumilated in bulk context
int KSyncSock::SendBulkMessage(KSyncBulkSandeshContext *bulk_context,
                               uint32_t seqno) {
    KSyncBufferList iovec;
    // Get all buffers to send into single io-vector
    bulk_context->Data(&iovec);
    tx_count_++;

    if (!read_inline_) {
        AsyncSendTo(&iovec, seqno,
                    boost::bind(&KSyncSock::WriteHandler, this,
                                placeholders::error,
                                placeholders::bytes_transferred));
    } else {
        SendTo(&iovec, seqno);
        bool more_data = false;
        do {
            int len = kBufLen;
            char *rxbuf = new char[len];
            Receive(boost::asio::buffer(rxbuf, kBufLen));
            more_data = IsMoreData(rxbuf);
            ValidateAndEnqueue(rxbuf);
        } while(more_data);
    }
    bulk_seq_no_ = -1;
    return true;
}

// Get the bulk-context for sequence-number
KSyncBulkSandeshContext *KSyncSock::LocateBulkContext(uint32_t seqno,
                              IoContext::IoContextWorkQId io_context_type) {
    tbb::mutex::scoped_lock lock(mutex_);
    if (bulk_seq_no_ == -1) {
        bulk_seq_no_ = seqno;
        bulk_buf_size_ = 0;
        bulk_msg_count_ = 0;
        wait_tree_.insert(WaitTreePair(seqno,
                              KSyncBulkSandeshContext(io_context_type)));
    }

    WaitTree::iterator it = wait_tree_.find(bulk_seq_no_);
    assert(it != wait_tree_.end());
    return &it->second;
}

// Try adding an io-context to bulk context. Returns
//  - true  : if message can be added to bulk context
//  - false : if message cannot be added to bulk context
bool KSyncSock::TryAddToBulk(KSyncBulkSandeshContext *bulk_context,
                             IoContext *ioc) {
    if ((bulk_buf_size_ + ioc->GetMsgLen()) > max_bulk_buf_size_)
        return false;

    if (bulk_msg_count_ >= max_bulk_msg_count_)
        return false;

    if (bulk_context->io_context_type() !=
        ioc->GetWorkQId())
        return false;

    bulk_buf_size_ += ioc->GetMsgLen();
    bulk_msg_count_++;

    bulk_context->Insert(ioc);
    return true;
}

bool KSyncSock::SendAsyncImpl(IoContext *ioc) {
    KSyncBulkSandeshContext *bulk_context = LocateBulkContext(ioc->GetSeqno(),
                                            ioc->GetWorkQId());
    // Try adding message to bulk-message list
    if (TryAddToBulk(bulk_context, ioc)) {
        // Message added to bulk-list. Nothing more to do
        return true;
    }

    // Message cannot be added to bulk-list. Send the current list
    SendBulkMessage(bulk_context, bulk_seq_no_);

    // Allocate a new context and add message to it
    bulk_context = LocateBulkContext(ioc->GetSeqno(),
                                     ioc->GetWorkQId());
    assert(TryAddToBulk(bulk_context, ioc));
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

void KSyncSockNetlink::Init(io_service &ios, int protocol) {
    KSyncSock::SetSockTableEntry(new KSyncSockNetlink(ios, protocol));
    KSyncSock::Init(false);
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
    KSyncBulkSandeshContext *bulk_context =
        dynamic_cast<KSyncBulkSandeshContext *>(ctxt);
    bulk_context->Decoder(buf, buf_len, NLA_ALIGNTO, more);
}

bool KSyncSockNetlink::BulkDecoder(char *data,
                                   KSyncBulkSandeshContext *bulk_context) {
    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetNetlinkPayload(data, &buf, &buf_len);
    return bulk_context->Decoder(buf, buf_len, NLA_ALIGNTO, IsMoreData(data));
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

void KSyncSockUdp::Init(io_service &ios, int port) {
    KSyncSock::SetSockTableEntry(new KSyncSockUdp(ios, port));
    KSyncSock::Init(false);
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
}

bool KSyncSockUdp::BulkDecoder(char *data,
                               KSyncBulkSandeshContext *bulk_context) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    uint32_t buf_len = hdr->msg_len;
    char *buf = data + sizeof(struct uvr_msg_hdr);
    return bulk_context->Decoder(buf, buf_len, 1, IsMoreData(data));
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

/////////////////////////////////////////////////////////////////////////////
// KSyncSockTcp routines
/////////////////////////////////////////////////////////////////////////////
//TCP socket class for interacting with vrouter
KSyncSockTcp::KSyncSockTcp(EventManager *evm,
    boost::asio::ip::address ip_address, int port) : TcpServer(evm), evm_(evm),
    session_(NULL), server_ep_(ip_address, port), connect_complete_(false) {
    session_ = CreateSession();
    Connect(session_, server_ep_);
}

void KSyncSockTcp::Init(EventManager *evm, boost::asio::ip::address ip_addr,
                        int port) {
    KSyncSock::SetSockTableEntry(new KSyncSockTcp(evm, ip_addr, port));
    SetNetlinkFamilyId(10);
    KSyncSock::Init(false);
}

TcpSession* KSyncSockTcp::AllocSession(Socket *socket) {
    TcpSession *session = new KSyncSockTcpSession(this, socket, false);
    session->set_observer(boost::bind(&KSyncSockTcp::OnSessionEvent,
                                      this, _1, _2));
    return session;
}

uint32_t KSyncSockTcp::GetSeqno(char *data) {
    return GetNetlinkSeqno(data);
}

bool KSyncSockTcp::IsMoreData(char *data) {
    return NetlinkMsgDone(data);
}

size_t KSyncSockTcp::SendTo(KSyncBufferList *iovec, uint32_t seq_no) {
    ResetNetlink(nl_client_);
    int offset = nl_client_->cl_buf_offset;
    UpdateNetlink(nl_client_, bulk_buf_size_, seq_no);

    KSyncBufferList::iterator it = iovec->begin();
    iovec->insert(it, buffer((char *)nl_client_->cl_buf, offset));

    uint32_t alloc_len = IoVectorLength(iovec);
    char msg[alloc_len];

    uint32_t len = IoVectorToData(msg, iovec);

    session_->Send((const uint8_t *)(msg), len, NULL);
    return nl_client_->cl_buf_offset;
}

void KSyncSockTcp::AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                               HandlerCb cb) {
    SendTo(iovec, seq_no);
    return;
}

bool KSyncSockTcp::Validate(char *data) {
    return ValidateNetlink(data);
}

bool KSyncSockTcp::Decoder(char *data, AgentSandeshContext *context) {
    KSyncSockNetlink::NetlinkDecoder(data, context);
    return true;
}

bool KSyncSockTcp::BulkDecoder(char *data,
                               KSyncBulkSandeshContext *bulk_context) {
    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetNetlinkPayload(data, &buf, &buf_len);
    return bulk_context->Decoder(buf, buf_len, NLA_ALIGNTO, IsMoreData(data));
}

void KSyncSockTcp::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    //Data would be read from ksync tcp session
    //hence no socket operation would be required
}

void KSyncSockTcp::Receive(mutable_buffers_1 buf) {
    uint32_t bytes_read = 0;
    boost::system::error_code ec;
    const struct nlmsghdr *nlh = NULL;

    //Create a buffer to read netlink header first
    mutable_buffers_1 netlink_header(buffer_cast<void *>(buf),
                                     sizeof(struct nlmsghdr));

    bool blocking_socket = session_->socket()->non_blocking();
    session_->socket()->non_blocking(false, ec);
    while (bytes_read < sizeof(struct nlmsghdr)) {
        mutable_buffers_1 buffer =
            static_cast<mutable_buffers_1>(netlink_header + bytes_read);
        bytes_read += session_->socket()->receive(buffer, 0, ec);
        if (ec != 0) {
            assert(0);
        }
        //Data read is lesser than netlink header
        //continue reading
        if (bytes_read == sizeof(struct nlmsghdr)) {
            nlh =  buffer_cast<struct nlmsghdr *>(buf);
        }
    }

    if (nlh->nlmsg_type == NLMSG_ERROR) {
        LOG(ERROR, "Netlink error for seqno " << nlh->nlmsg_seq
                << " len " << nlh->nlmsg_len);
        assert(0);
    }

    bytes_read = 0;
    uint32_t payload_size = nlh->nlmsg_len - sizeof(struct nlmsghdr);
    //Read data
    mutable_buffers_1 data(buffer_cast<void *>(buf + sizeof(struct nlmsghdr)),
                           payload_size);

    while (bytes_read < payload_size) {
        mutable_buffers_1 buffer =
            static_cast<mutable_buffers_1>(data + bytes_read);
        bytes_read += session_->socket()->receive(buffer, 0, ec);
        if (ec != 0) {
            assert(0);
        }
    }
    session_->socket()->non_blocking(blocking_socket, ec);
}

bool KSyncSockTcp::ReceiveMsg(const u_int8_t *msg, size_t size) {
    char *rx_buff = new char[kBufLen];
    memcpy(rx_buff, msg, size);
    ValidateAndEnqueue(rx_buff);
    return true;
}

void KSyncSockTcp::AsyncReadStart() {
    session_->AsyncReadStart();
}

void KSyncSockTcp::OnSessionEvent(TcpSession *session,
                                  TcpSession::Event event) {
    switch (event) {
    case TcpSession::CONNECT_FAILED:
        //Retry
        Connect(session_, server_ep_);
        break;
    case TcpSession::CLOSE:
        LOG(ERROR, "Connection to dpdk-vrouter lost.");
        sleep(10);
        exit(EXIT_FAILURE);
        break;
    case TcpSession::CONNECT_COMPLETE:
        connect_complete_ = true;
    default:
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////
// KSyncSockTcpSession routines
/////////////////////////////////////////////////////////////////////////////
KSyncSockTcpSession::KSyncSockTcpSession(TcpServer *server, Socket *sock,
    bool async_ready) : TcpSession(server, sock, async_ready) {
    KSyncSockTcp *tcp_ptr = static_cast<KSyncSockTcp *>(server);
    reader_ = new KSyncSockTcpSessionReader(this,
                       boost::bind(&KSyncSockTcp::ReceiveMsg, tcp_ptr, _1, _2));
}

void KSyncSockTcpSession::OnRead(Buffer buffer) {
    reader_->OnRead(buffer);
}

KSyncSockTcpSessionReader::KSyncSockTcpSessionReader(
    TcpSession *session, ReceiveCallback callback) :
    TcpMessageReader(session, callback) {
}

int KSyncSockTcpSessionReader::MsgLength(Buffer buffer, int offset) {
    size_t size = TcpSession::BufferSize(buffer);
    int remain = size - offset;
    if (remain < GetHeaderLenSize()) {
        return -1;
    }

    //Byte ordering?
    const struct nlmsghdr *nlh =
        (const struct nlmsghdr *)(TcpSession::BufferData(buffer) + offset);
    return nlh->nlmsg_len;
}

/////////////////////////////////////////////////////////////////////////////
// KSyncIoContext routines
/////////////////////////////////////////////////////////////////////////////
KSyncIoContext::KSyncIoContext(KSyncEntry *sync_entry, int msg_len, char *msg,
                               uint32_t seqno, KSyncEntry::KSyncEvent event) :
    IoContext(msg, msg_len, seqno, KSyncSock::GetAgentSandeshContext(),
              IoContext::DEFAULT_Q_ID), entry_(sync_entry), event_(event) {
}

void KSyncIoContext::Handler() {
    if (KSyncObject *obj = entry_->GetObject()) {
        obj->NetlinkAck(entry_, event_);
    }
}

void KSyncIoContext::ErrorHandler(int err) {
    entry_->ErrorHandler(err, GetSeqno());
}

/////////////////////////////////////////////////////////////////////////////
// Routines for KSyncBulkSandeshContext
/////////////////////////////////////////////////////////////////////////////
KSyncBulkSandeshContext::KSyncBulkSandeshContext
(IoContext::IoContextWorkQId io_context_type) :
    AgentSandeshContext(), vr_response_count_(0), io_context_list_it_(),
    io_context_list_(), io_context_type_(io_context_type) {
}

KSyncBulkSandeshContext::KSyncBulkSandeshContext
(const KSyncBulkSandeshContext &rhs) :
    AgentSandeshContext(), vr_response_count_(0), io_context_list_it_(),
    io_context_list_(), io_context_type_(rhs.io_context_type_) {
}

struct IoContextDisposer {
    void operator() (IoContext *io_context) { delete io_context; }
};

KSyncBulkSandeshContext::~KSyncBulkSandeshContext() {
    assert(vr_response_count_ == io_context_list_.size());
    io_context_list_.clear_and_dispose(IoContextDisposer());
}

void KSyncBulkSandeshContext::Insert(IoContext *ioc) {
    io_context_list_.push_back(*ioc);
    return;
}

void KSyncBulkSandeshContext::Data(KSyncBufferList *iovec) {
    IoContextList::iterator it = io_context_list_.begin();
    while (it != io_context_list_.end()) {
        iovec->push_back(buffer(it->GetMsg(), it->GetMsgLen()));
        it++;
    }
}

// Sandesh responses for old context are done. Check for any errors
void KSyncBulkSandeshContext::IoContextDone() {
    IoContext *io_context = &(*io_context_list_it_);
    AgentSandeshContext *sandesh_context = io_context->GetSandeshContext();

    sandesh_context->set_ksync_io_ctx(NULL);
    if (sandesh_context->GetErrno() != 0 &&
        sandesh_context->GetErrno() != EEXIST) {
        io_context->ErrorHandler(sandesh_context->GetErrno());
    }
    io_context->Handler();
}

void KSyncBulkSandeshContext::IoContextStart() {
    vr_response_count_++;
    IoContext &io_context = *io_context_list_it_;
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
    assert(io_context_list_it_ != io_context_list_.end());
    if (more == true)
        return false;

    IoContextDone();

    // No more netlink messages. Validate that iterator points to last element
    // in IoContextList
    io_context_list_it_++;
    assert(io_context_list_it_ == io_context_list_.end());
    return true;
}

void KSyncBulkSandeshContext::SetErrno(int err) {
    AgentSandeshContext *context = GetSandeshContext();
    context->SetErrno(err);
}

AgentSandeshContext *KSyncBulkSandeshContext::GetSandeshContext() {
    assert(vr_response_count_);
    return io_context_list_it_->GetSandeshContext();
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

// vr_response message is treated as delimiter in a bulk-context. So, move to
// next io-context within bulk-message context.
int KSyncBulkSandeshContext::VrResponseMsgHandler(vr_response *resp) {
    AgentSandeshContext *sandesh_context = NULL;
    // If this is first vr_reponse received, move io-context to first entry in
    // bulk context
    if (vr_response_count_ == 0) {
        io_context_list_it_ = io_context_list_.begin();
        sandesh_context = io_context_list_it_->GetSandeshContext();
        IoContextStart();
    } else {
        // Sandesh responses for old io-context are done.
        // Check for any errors and trigger state-machine for old io-context
        IoContextDone();
        // Move to the next io-context
        io_context_list_it_++;
        assert(io_context_list_it_ != io_context_list_.end());
        sandesh_context = io_context_list_it_->GetSandeshContext();
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

void KSyncBulkSandeshContext::VrfAssignMsgHandler(vr_vrf_assign_req *req) {
    AgentSandeshContext *context = GetSandeshContext();
    context->VrfAssignMsgHandler(req);
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
