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
std::vector<KSyncSock *> KSyncSock::sock_table_;
pid_t KSyncSock::pid_;
tbb::atomic<bool> KSyncSock::shutdown_;

const char* IoContext::io_wq_names[IoContext::MAX_WORK_QUEUES] = 
                                                {"Agent::KSync", "Agent::Uve"};

/////////////////////////////////////////////////////////////////////////////
// Netlink utilities
/////////////////////////////////////////////////////////////////////////////
static uint32_t GetNetlinkSeqno(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return nlh->nlmsg_seq;
}

static bool NetlinkMsgDone(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return ((nlh->nlmsg_flags & NLM_F_MULTI) &&
            (nlh->nlmsg_type != NLMSG_DONE));
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

static void GetSandeshMessage(char *data, char **buf, uint32_t *buf_len) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    *buf = data + NLMSG_HDRLEN + GENL_HDRLEN + NLA_HDRLEN;
    *buf_len = nlh->nlmsg_len - (NLMSG_HDRLEN + GENL_HDRLEN + NLA_HDRLEN);
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
                                  SandeshContext *sandesh_context) {
    while (buf_len > (NLA_ALIGNTO - 1)) {
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
KSyncSock::KSyncSock() : tx_count_(0), err_count_(0), read_inline_(true) {
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
    async_send_queue_ =
        new WorkQueue<IoContext *>(task_id, 0,
                                   boost::bind(&KSyncSock::SendAsyncImpl, this,
                                               _1));
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

    assert(async_send_queue_->Length() == 0);
    async_send_queue_->Shutdown();
    delete async_send_queue_;

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
    STLDeleteValues(&sock_table_);
}

void KSyncSock::Init(int count) {
    sock_table_.resize(count);
    pid_ = getpid();
    shutdown_ = false;
}

void KSyncSock::Start(bool read_inline) {
    for (std::vector<KSyncSock *>::iterator it = sock_table_.begin();
         it != sock_table_.end(); ++it) {
        (*it)->read_inline_ = read_inline;
        if ((*it)->read_inline_) {
            continue;
        }
        (*it)->async_send_queue_->SetStartRunnerFunc(
                boost::bind(&KSyncSock::SendAsyncStart, *it));
        (*it)->rx_buff_ = new char[kBufLen];
        (*it)->AsyncReceive(boost::asio::buffer((*it)->rx_buff_, kBufLen),
                            boost::bind(&KSyncSock::ReadHandler, *it,
                                        placeholders::error,
                                        placeholders::bytes_transferred));
    }
}

void KSyncSock::SetSockTableEntry(int i, KSyncSock *sock) {
    sock_table_[i] = sock;
}

void KSyncSock::SetNetlinkFamilyId(int id) {
    vnsw_netlink_family_id_ = id;
    for (std::vector<KSyncSock *>::iterator it = sock_table_.begin();
         it != sock_table_.end(); it++) {
        KSyncSockNetlink *sock = dynamic_cast<KSyncSockNetlink *>(*it);
        if (sock) {
            InitNetlink(sock->nl_client_);
        }
    }
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
    int idx = partition->index();
    return sock_table_[idx];
}

KSyncSock *KSyncSock::Get(int idx) {
    return sock_table_[idx];
}

Tree::iterator KSyncSock::GetIoContext(char *data) {
    IoContext ioc;
    ioc.SetSeqno(GetNetlinkSeqno(data));
    Tree::iterator it;
    {
        tbb::mutex::scoped_lock lock(mutex_);
        it = wait_tree_.find(ioc);
    }
    assert (it != wait_tree_.end());
    return it;
}

bool KSyncSock::ValidateAndEnqueue(char *data) {
    Validate(data);
    IoContext::IoContextWorkQId q_id;
    if ((GetNetlinkSeqno(data) & KSYNC_DEFAULT_Q_ID_SEQ) ==
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
    Tree::iterator it = GetIoContext(data);
    IoContext *context = it.operator->();

    AgentSandeshContext *ctxt = context->GetSandeshContext();
    ctxt->SetErrno(0);
    ctxt->set_ksync_io_ctx(static_cast<KSyncIoContext *>(context));
    Decoder(data, ctxt);
    ctxt->set_ksync_io_ctx(NULL);
    if (ctxt->GetErrno() != 0) {
        context->ErrorHandler(ctxt->GetErrno());
    }

    if (!IsMoreData(data)) {
        context->Handler();
        {
            tbb::mutex::scoped_lock lock(mutex_);
            wait_tree_.erase(it);
        }
        async_send_queue_->MayBeStartRunner();
        delete(context);
    }

    delete[] data;
    return true;
}
    
bool KSyncSock::BlockingRecv() {
    char *data = new char[kBufLen];
    bool ret = false;

    do {
        Receive(boost::asio::buffer(data, kBufLen));

        AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext();
        ctxt->SetErrno(0);
        Decoder(data, ctxt);
        if (ctxt->GetErrno() != 0) {
            KSYNC_ERROR(VRouterError, "VRouter operation failed. Error <", 
                        ctxt->GetErrno(), ":", strerror(ctxt->GetErrno()), 
                        ">. Object <", "N/A", ">. State <", "N/A",
                        ">. Message number :", 0);
            ret = true;
        }
    } while (IsMoreData(data));

    delete [] data;
    return ret;
}

size_t KSyncSock::BlockingSend(const char *msg, int msg_len) {
    return SendTo(msg, msg_len, 0);
}

void KSyncSock::GenericSend(IoContext *ioc) {
    async_send_queue_->Enqueue(ioc);
}

void KSyncSock::SendAsync(KSyncEntry *entry, int msg_len, char *msg,
                          KSyncEntry::KSyncEvent event) {
    uint32_t seq = AllocSeqNo(false);
    KSyncIoContext *ioc = new KSyncIoContext(entry, msg_len, msg, seq, event);
    async_send_queue_->Enqueue(ioc);
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

bool KSyncSock::SendAsyncImpl(IoContext *ioc) {
    {
        tbb::mutex::scoped_lock lock(mutex_);
        wait_tree_.insert(*ioc);
    }
    if (read_inline_ == false) {
        AsyncSendTo(ioc->GetMsg(), ioc->GetMsgLen(), ioc->GetSeqno(),
                    boost::bind(&KSyncSock::WriteHandler, this,
                                placeholders::error,
                                placeholders::bytes_transferred));
    } else {
        SendTo((const char *)ioc->GetMsg(),
               ioc->GetMsgLen(), ioc->GetSeqno());
        bool more_data = false;
        do {
            int len = kBufLen;
            char *rxbuf = new char[len];
            Receive(boost::asio::buffer(rxbuf, kBufLen));
            more_data = IsMoreData(rxbuf);
            ValidateAndEnqueue(rxbuf);
        } while(more_data);
    }
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

void KSyncSockNetlink::Init(io_service &ios, int count, int protocol) {
    KSyncSock::Init(count);
    for (int i = 0; i < count; i++) {
        KSyncSock::SetSockTableEntry(i, new KSyncSockNetlink(ios, protocol));
    }
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
void KSyncSockNetlink::AsyncSendTo(char *data, uint32_t data_len,
                                   uint32_t seq_no, HandlerCb cb) {
    ResetNetlink(nl_client_);
    std::vector<mutable_buffers_1> iovec;
    iovec.push_back(buffer(nl_client_->cl_buf, nl_client_->cl_buf_offset));
    iovec.push_back(buffer(data, data_len));
    UpdateNetlink(nl_client_, data_len, seq_no);

    boost::asio::netlink::raw::endpoint ep;
    sock_.async_send_to(iovec, ep, cb);
}

size_t KSyncSockNetlink::SendTo(const char *data, uint32_t data_len,
                                 uint32_t seq_no) {
    ResetNetlink(nl_client_);
    std::vector<const_buffers_1> iovec;
    iovec.push_back(buffer((const char *)nl_client_->cl_buf,
                           nl_client_->cl_buf_offset));
    iovec.push_back(buffer((const char *)data, data_len));
    UpdateNetlink(nl_client_, data_len, seq_no);

    boost::asio::netlink::raw::endpoint ep;
    return sock_.send_to(iovec, ep);
}

void KSyncSockNetlink::Decoder(char *data, SandeshContext *sandesh_context) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    if (nlh->nlmsg_type == NLMSG_DONE) {
        return;
    }

    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetSandeshMessage(data, &buf, &buf_len);
    DecodeSandeshMessages(buf, buf_len, sandesh_context);
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
    sock_(ios, ip::udp::endpoint(ip::udp::v4(), 0)),
    server_ep_(ip::address::from_string("127.0.0.1"), port) {
}

void KSyncSockUdp::Init(io_service &ios, int count, int port) {
    KSyncSock::Init(count);
    for (int i = 0; i < count; i++) {
        KSyncSock::SetSockTableEntry(i, new KSyncSockUdp(ios, port));
    }
}

uint32_t KSyncSockUdp::GetSeqno(char *data) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    return hdr->seq_no;
}

bool KSyncSockUdp::IsMoreData(char *data) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    return ((hdr->flags & UVR_MORE) == UVR_MORE);
}

void KSyncSockUdp::Decoder(char *data, SandeshContext *sandesh_context) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    uint32_t buf_len = hdr->msg_len;
    char *buf = data + sizeof(struct uvr_msg_hdr);
    DecodeSandeshMessages(buf, buf_len, sandesh_context);
}

void KSyncSockUdp::AsyncSendTo(char *data, uint32_t data_len,
                               uint32_t seq_no, HandlerCb cb) {
    struct uvr_msg_hdr hdr;
    std::vector<mutable_buffers_1> iovec;
    hdr.seq_no = seq_no;
    hdr.flags = 0;
    hdr.msg_len = data_len;

    iovec.push_back(buffer((char *)(&hdr), sizeof(hdr)));
    iovec.push_back(buffer(data, data_len));

    sock_.async_send_to(iovec, server_ep_, cb);
}

size_t KSyncSockUdp::SendTo(const char *data, uint32_t data_len,
                            uint32_t seq_no) {
    struct uvr_msg_hdr hdr;
    std::vector<const_buffers_1> iovec;
    hdr.seq_no = seq_no;
    hdr.flags = 0;
    hdr.msg_len = data_len;

    iovec.push_back(buffer((const char *)(&hdr), sizeof(hdr)));
    iovec.push_back(buffer((const char *)data, data_len));

    size_t ret = sock_.send_to(iovec, server_ep_, MSG_DONTWAIT);
    return ret;
}

bool KSyncSockUdp::Validate(char *data) {
    return true;
}

void KSyncSockUdp::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    ip::udp::endpoint ep;
    sock_.async_receive_from(buf, ep, cb);
}

void KSyncSockUdp::Receive(mutable_buffers_1 buf) {
    ip::udp::endpoint ep;
    sock_.receive_from(buf, ep);
}

/////////////////////////////////////////////////////////////////////////////
// KSyncSockTcp routines
/////////////////////////////////////////////////////////////////////////////
//TCP socket class for interacting with vrouter
KSyncSockTcp::KSyncSockTcp(EventManager *evm,
    ip::address ip_address, int port) : TcpServer(evm), evm_(evm),
    session_(NULL), server_ep_(ip_address, port), connect_complete_(false) {
    session_ = CreateSession();
    Connect(session_, server_ep_);
}

void KSyncSockTcp::Init(EventManager *evm, int count, ip::address ip_addr,
                        int port) {
    KSyncSock::Init(count);
    SetNetlinkFamilyId(10);
    for (int i = 0; i < count; i++) {
        KSyncSock::SetSockTableEntry(i, new KSyncSockTcp(evm, ip_addr, port));
    }
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

void KSyncSockTcp::AsyncSendTo(char *data, uint32_t data_len,
                               uint32_t seq_no, HandlerCb cb) {
    ResetNetlink(nl_client_);
    UpdateNetlink(nl_client_, data_len, seq_no);

    uint32_t total_length = nl_client_->cl_buf_offset + data_len;
    assert(total_length < 4096);
    char msg[4096];
    memcpy(msg, nl_client_->cl_buf, nl_client_->cl_buf_offset);
    memcpy(msg + nl_client_->cl_buf_offset, data, data_len);

    session_->Send((const uint8_t *)msg, total_length, NULL);
}

size_t KSyncSockTcp::SendTo(const char *data, uint32_t data_len,
                            uint32_t seq_no) {
    ResetNetlink(nl_client_);
    UpdateNetlink(nl_client_, data_len, seq_no);

    uint32_t total_length = nl_client_->cl_buf_offset + data_len;
    assert(total_length < 4096);
    char msg[4096];
    memcpy(msg, nl_client_->cl_buf, nl_client_->cl_buf_offset);
    memcpy(msg + nl_client_->cl_buf_offset, data, data_len);

    session_->Send((const uint8_t *)msg, total_length, NULL);
    return total_length;
}

bool KSyncSockTcp::Validate(char *data) {
    return ValidateNetlink(data);
}

void KSyncSockTcp::Decoder(char *data, SandeshContext *sandesh_context) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    if (nlh->nlmsg_type == NLMSG_DONE) {
        return;
    }

    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetSandeshMessage(data, &buf, &buf_len);
    DecodeSandeshMessages(buf, buf_len, sandesh_context);
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
    if (KSyncObject *obj = entry_->GetObject())
        obj->NetlinkAck(entry_, event_);
}

void KSyncIoContext::ErrorHandler(int err) {
    entry_->ErrorHandler(err, GetSeqno());
}
