/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#if defined(__linux__)
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/sockios.h>
#elif defined(__FreeBSD__)
#include "vr_os.h"
#endif

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
#if defined(__linux__)
        SO_RCVBUFFORCE> ReceiveBuffForceSize;
#else 
        SO_RCVBUF> ReceiveBuffForceSize;
#endif

int KSyncSock::vnsw_netlink_family_id_;
AgentSandeshContext *KSyncSock::agent_sandesh_ctx_;
std::vector<KSyncSock *> KSyncSock::sock_table_;
pid_t KSyncSock::pid_;
tbb::atomic<bool> KSyncSock::shutdown_;

const char* IoContext::io_wq_names[IoContext::MAX_WORK_QUEUES] = 
                                                {"Agent::KSync", "Agent::Uve"};

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

uint32_t KSyncSockNetlink::GetSeqno(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    return nlh->nlmsg_seq;
}

bool KSyncSockNetlink::IsMoreData(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;

    return ((nlh->nlmsg_flags & NLM_F_MULTI) && (nlh->nlmsg_type != NLMSG_DONE));
}

void KSyncSockNetlink::Decoder(char *data, SandeshContext *ctxt) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    //LOG(DEBUG, "Kernel Data: msg_type " << nlh->nlmsg_type << " seq no " 
    //                << nlh->nlmsg_seq << " len " << nlh->nlmsg_len);
    if (nlh->nlmsg_type == GetNetlinkFamilyId()) {
        struct genlmsghdr *genlh = (struct genlmsghdr *)
                                   (data + NLMSG_HDRLEN);
        int total_len = nlh->nlmsg_len;
        int decode_len;
        uint8_t *decode_buf;
        if (genlh->cmd == SANDESH_REQUEST) {
            struct nlattr * attr = (struct nlattr *)(data + NLMSG_HDRLEN
                                                     + GENL_HDRLEN);
            int decode_buf_len = total_len - (NLMSG_HDRLEN + GENL_HDRLEN + 
                                              NLA_HDRLEN);
            int err = 0;
            if (attr->nla_type == NL_ATTR_VR_MESSAGE_PROTOCOL) {
                decode_buf = (uint8_t *)(data + NLMSG_HDRLEN + 
                                         GENL_HDRLEN + NLA_HDRLEN);
                while(decode_buf_len > (NLA_ALIGNTO - 1)) {
                    decode_len = Sandesh::ReceiveBinaryMsgOne(decode_buf, decode_buf_len, &err,
                                                              ctxt);
                    if (decode_len < 0) {
                        LOG(DEBUG, "Incorrect decode len " << decode_len);
                        break;
                    }
                    decode_buf += decode_len;
                    decode_buf_len -= decode_len;
                }
            } else {
                LOG(ERROR, "Unknown generic netlink TLV type : " << attr->nla_type);
                assert(0);
            }
        } else {
            LOG(ERROR, "Unknown generic netlink cmd : " << genlh->cmd);
            assert(0);
        }
    } else if (nlh->nlmsg_type != NLMSG_DONE) {
        LOG(ERROR, "Netlink unknown message type : " << nlh->nlmsg_type);
        assert(0);
    }
}

bool KSyncSockNetlink::Validate(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    if (nlh->nlmsg_type == NLMSG_ERROR) {
        LOG(ERROR, "Ignoring Netlink error for seqno " << nlh->nlmsg_seq 
                        << " len " << nlh->nlmsg_len);
        assert(0);
        return true;
    }

    if (nlh->nlmsg_len > kBufLen) {
        LOG(ERROR, "Length of " << nlh->nlmsg_len << " is more than expected "
            "length of " << kBufLen);
        assert(0);
        return true;
    }
    return true;
}

//netlink socket class for interacting with kernel
void KSyncSockNetlink::AsyncSendTo(IoContext *ioc, mutable_buffers_1 buf,
                                   HandlerCb cb) {
    struct nl_client cl;
    unsigned char *nl_buf;
    uint32_t nl_buf_len;
    int ret;
    std::vector<mutable_buffers_1> iovec;

    nl_init_generic_client_req(&cl, GetNetlinkFamilyId());

    if ((ret = nl_build_header(&cl, &nl_buf, &nl_buf_len)) < 0) {
        LOG(ERROR, "Error creating netlink message. Error : " << ret);
        free(cl.cl_buf);
        return;
    }

    iovec.push_back(buffer(cl.cl_buf, cl.cl_buf_offset));
    iovec.push_back(buf);

    nl_update_header(&cl, buffer_size(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)cl.cl_buf;
    nlh->nlmsg_pid = KSyncSock::GetPid();
    nlh->nlmsg_seq = ioc->GetSeqno();

    boost::asio::netlink::raw::endpoint ep;
    sock_.async_send_to(iovec, ep, cb);
    free(cl.cl_buf);
}

size_t KSyncSockNetlink::SendTo(const_buffers_1 buf, uint32_t seq_no) {
    struct nl_client cl;
    unsigned char *nl_buf;
    uint32_t nl_buf_len;
    int ret;
    std::vector<const_buffers_1> iovec;

    nl_init_generic_client_req(&cl, GetNetlinkFamilyId());

    if ((ret = nl_build_header(&cl, &nl_buf, &nl_buf_len)) < 0) {
        LOG(ERROR, "Error creating netlink message. Error : " << ret);
        free(cl.cl_buf);
        return ((size_t) -1);
    }

    iovec.push_back(buffer((const char *)cl.cl_buf, cl.cl_buf_offset));
    iovec.push_back(buf);

    nl_update_header(&cl, buffer_size(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)cl.cl_buf;
    nlh->nlmsg_pid = KSyncSock::GetPid();
    nlh->nlmsg_seq = seq_no;

    boost::asio::netlink::raw::endpoint ep;
    size_t ret_val = sock_.send_to(iovec, ep);
    free(cl.cl_buf);
    return ret_val;
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

//Udp socket class for interacting with kernel
KSyncSockUdp::KSyncSockUdp(boost::asio::io_service &ios, int port)
    : sock_(ios, ip::udp::endpoint(ip::udp::v4(), 0)), server_ep_(ip::address::from_string("127.0.0.1"), port) {
    //sock_.open(ip::udp::v4());
}

uint32_t KSyncSockUdp::GetSeqno(char *data) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    return hdr->seq_no;
}

bool KSyncSockUdp::IsMoreData(char *data) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    return ((hdr->flags & UVR_MORE) == UVR_MORE);
}

void KSyncSockUdp::Decoder(char *data, SandeshContext *ctxt) {
    struct uvr_msg_hdr *hdr = (struct uvr_msg_hdr *)data;
    int decode_len;
    int decode_buf_len = hdr->msg_len;
    uint8_t *decode_buf;
    int err = 0;
    decode_buf = (uint8_t *)(data + sizeof(struct uvr_msg_hdr));
    while(decode_buf_len > 0) {
        decode_len = Sandesh::ReceiveBinaryMsgOne(decode_buf, decode_buf_len,
                                                  &err, ctxt);
        if (decode_len < 0) {
            LOG(DEBUG, "Incorrect decode len " << decode_len);
            break;
        }
        decode_buf += decode_len;
        decode_buf_len -= decode_len;
    }
}

bool KSyncSockUdp::Validate(char *data) {
    return true;
}

void KSyncSockUdp::AsyncSendTo(IoContext *ioc, mutable_buffers_1 buf,
                               HandlerCb cb) {
    struct uvr_msg_hdr hdr;
    std::vector<mutable_buffers_1> iovec;
    hdr.seq_no = ioc->GetSeqno();
    hdr.flags = 0;
    hdr.msg_len = buffer_size(buf);

    iovec.push_back(buffer((char *)(&hdr), sizeof(hdr)));
    iovec.push_back(buf);

    sock_.async_send_to(iovec, server_ep_, cb);
}

size_t KSyncSockUdp::SendTo(const_buffers_1 buf, uint32_t seq_no) {
    struct uvr_msg_hdr hdr;
    std::vector<const_buffers_1> iovec;
    hdr.seq_no = seq_no;
    hdr.flags = 0;
    hdr.msg_len = buffer_size(buf);

    iovec.push_back(buffer((const char *)(&hdr), sizeof(hdr)));
    iovec.push_back(buf);

    size_t ret = sock_.send_to(iovec, server_ep_, MSG_DONTWAIT);
    return ret;
}

void KSyncSockUdp::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    ip::udp::endpoint ep;
    sock_.async_receive_from(buf, ep, cb);
}

void KSyncSockUdp::Receive(mutable_buffers_1 buf) {
    ip::udp::endpoint ep;
    sock_.receive_from(buf, ep);
}

KSyncSock::KSyncSock() : tx_count_(0), err_count_(0), run_sync_mode_(true) {
    for(int i = 0; i < IoContext::MAX_WORK_QUEUES; i++) {
        receive_work_queue[i] = new WorkQueue<char *>(TaskScheduler::GetInstance()->
                             GetTaskId(IoContext::io_wq_names[i]), 0,
                             boost::bind(&KSyncSock::ProcessKernelData, this, 
                                         _1));
    }
    async_send_queue_ = new WorkQueue<IoContext *>(TaskScheduler::GetInstance()->
                            GetTaskId("Ksync::AsyncSend"), 0,
                            boost::bind(&KSyncSock::SendAsyncImpl, this, _1));
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
}

void KSyncSock::Start(bool run_sync_mode) {
    for (std::vector<KSyncSock *>::iterator it = sock_table_.begin();
         it != sock_table_.end(); ++it) {
        (*it)->run_sync_mode_ = run_sync_mode;
        if ((*it)->run_sync_mode_) {
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

void KSyncSock::Init(int count) {
    sock_table_.resize(count);
    pid_ = getpid();
    shutdown_ = false;
}

void KSyncSockNetlink::Init(io_service &ios, int count, int protocol) {
    KSyncSock::Init(count);
    for (int i = 0; i < count; i++) {
        KSyncSock::SetSockTableEntry(i, new KSyncSockNetlink(ios, protocol));
    }
}

void KSyncSockUdp::Init(io_service &ios, int count, int port) {
    KSyncSock::Init(count);
    for (int i = 0; i < count; i++) {
        KSyncSock::SetSockTableEntry(i, new KSyncSockUdp(ios, port));
    }
}

// Create KSyncSock objects
void KSyncSock::Shutdown() {
    shutdown_ = true;
    STLDeleteValues(&sock_table_);
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

Tree::iterator KSyncSock::GetIoContext(char *data) {
    IoContext ioc;
    ioc.SetSeqno(GetSeqno(data));
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
    if ((GetSeqno(data) & KSYNC_DEFAULT_Q_ID_SEQ) == KSYNC_DEFAULT_Q_ID_SEQ) {
        q_id = IoContext::DEFAULT_Q_ID;
    } else {
        q_id = IoContext::UVE_Q_ID;
    }
    receive_work_queue[q_id]->Enqueue(data);
    return true;
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

KSyncSock *KSyncSock::Get(DBTablePartBase *partition) {
    int idx = partition->index();
    return sock_table_[idx];
}

KSyncSock *KSyncSock::Get(int idx) {
    return sock_table_[idx];
}

size_t KSyncSock::BlockingSend(const char *msg, int msg_len) {
    return SendTo(buffer(msg, msg_len), 0);
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

void KSyncSock::SendAsync(KSyncEntry *entry, int msg_len, char *msg,
                          KSyncEntry::KSyncEvent event) {
    uint32_t seq = AllocSeqNo(false);
    KSyncIoContext *ioc = new KSyncIoContext(entry, msg_len, msg, seq, event);
    async_send_queue_->Enqueue(ioc);
}

void KSyncSock::GenericSend(IoContext *ioc) {
    async_send_queue_->Enqueue(ioc);
}

bool KSyncSock::SendAsyncImpl(IoContext *ioc) {
    {
        tbb::mutex::scoped_lock lock(mutex_);
        wait_tree_.insert(*ioc);
    }
    if (!run_sync_mode_) {
        AsyncSendTo(ioc, boost::asio::buffer(ioc->GetMsg(), ioc->GetMsgLen()),
                    boost::bind(&KSyncSock::WriteHandler, this,
                                placeholders::error,
                                placeholders::bytes_transferred));
    } else {
        SendTo(boost::asio::buffer((const char *)ioc->GetMsg(),
                    ioc->GetMsgLen()), ioc->GetSeqno());
        bool more_data = false;
        do {
            char *rxbuf = new char[kBufLen];
            Receive(boost::asio::buffer(rxbuf, kBufLen));
            more_data = IsMoreData(rxbuf);
            ValidateAndEnqueue(rxbuf);
        } while(more_data);
    }
    return true;
}

KSyncIoContext::KSyncIoContext(KSyncEntry *sync_entry, int msg_len,
                               char *msg, uint32_t seqno,
                               KSyncEntry::KSyncEvent event) :
    IoContext(msg, msg_len, seqno, KSyncSock::GetAgentSandeshContext()), 
        entry_(sync_entry), event_(event) { 
}

void KSyncIoContext::Handler() {
    if (KSyncObject *obj = entry_->GetObject())
        obj->NetlinkAck(entry_, event_);
}

void KSyncIoContext::ErrorHandler(int err) {
    entry_->ErrorHandler(err, GetSeqno());
}

