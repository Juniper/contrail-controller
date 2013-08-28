/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/sockios.h>

#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <common/vns_constants.h>
#include <common/vns_types.h>

#include "ksync_index.h"
#include "ksync_entry.h"
#include "ksync_object.h"
#include "ksync_sock.h"
#include "ksync_sock_user.h"
#include "ksync_types.h"

#include "nl_util.h"
#include "vr_genetlink.h"
#include "vr_types.h"

using namespace boost::asio;

#define KSYNC_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::VROUTER)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

int KSyncSock::vnsw_netlink_family_id_;
AgentSandeshContext *KSyncSock::agent_sandesh_ctx_;
std::vector<KSyncSock *> KSyncSock::sock_table_;
pid_t KSyncSock::pid_;
tbb::atomic<bool> KSyncSock::shutdown_;

const char* IoContext::io_wq_names[IoContext::MAX_WORK_QUEUES] = 
                                                {"Agent::KSync", "Agent::Uve"};

KSyncSockTypeNetlink::KSyncSockTypeNetlink(boost::asio::io_service &ios,
                                           int protocol) 
    : sock_(ios, protocol) {
}

//netlink socket class for interacting with kernel
void KSyncSockTypeNetlink::AsyncSendTo(mutable_buffers_1 buf, HandlerCb cb) {
    boost::asio::netlink::raw::endpoint ep;
    sock_.async_send_to(buf, ep, cb);
}

void KSyncSockTypeNetlink::SendTo(const_buffers_1 buf) {
    boost::asio::netlink::raw::endpoint ep;
    sock_.send_to(buf, ep);
}

void KSyncSockTypeNetlink::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    sock_.async_receive(buf, cb);
}

void KSyncSockTypeNetlink::Receive(mutable_buffers_1 buf) {
    sock_.receive(buf);
}

KSyncSock::KSyncSock(io_service &ios, int protocol) : 
    tx_count_(0), err_count_(0) {
    for(int i = 0; i < IoContext::MAX_WORK_QUEUES; i++) {
        work_queue_[i] = new WorkQueue<char *>(TaskScheduler::GetInstance()->
                             GetTaskId(IoContext::io_wq_names[i]), 0,
                             boost::bind(&KSyncSock::ProcessKernelData, this, 
                                         _1));
    }
    seqno_ = 0;
    if (protocol == NETLINK_GENERIC) {
        sock_type_ = new KSyncSockTypeNetlink(ios, protocol);
    } else  {
        sock_type_ = KSyncSockTypeMap::GetKSyncSockTypeMap();
    }
}

KSyncSock::~KSyncSock() {
    assert(wait_tree_.size() == 0);

    delete sock_type_;
    sock_type_ = NULL;
    if (rx_buff_) {
        delete [] rx_buff_;
        rx_buff_ = NULL;
    }

    for(int i = 0; i < IoContext::MAX_WORK_QUEUES; i++) {
        work_queue_[i]->Shutdown();
        delete work_queue_[i];
    }
}

void KSyncSock::Start() {
    for (std::vector<KSyncSock *>::iterator it = sock_table_.begin();
         it != sock_table_.end(); ++it) {
        (*it)->rx_buff_ = new char[kBufLen];
        (*it)->sock_type_->AsyncReceive(
                boost::asio::buffer((*it)->rx_buff_, kBufLen),
                boost::bind(&KSyncSock::ReadHandler, *it, placeholders::error,
                            placeholders::bytes_transferred));
    }
}

// Create KSyncSock objects
void KSyncSock::Init(io_service &ios, int count, int protocol) {
    //init userspace map, udp, file....
    if (protocol != NETLINK_GENERIC) {
        KSyncSockTypeMap::Init(ios);
    }

    sock_table_.resize(count);
    for (int i = 0; i < count; i++) {
        sock_table_[i] = new KSyncSock(ios, protocol);
    }
    pid_ = getpid();
    shutdown_ = false;
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
        LOG(ERROR, "Error reading from Netlink sock. Error : " << 
            boost::system::system_error(error).what());
        if (shutdown_ == false) {
            assert(0);
        }
        return;
    }

    ValidateAndEnqueue(rx_buff_);

    rx_buff_ = new char[kBufLen];
    sock_type_->AsyncReceive(boost::asio::buffer(rx_buff_, kBufLen),
                             boost::bind(&KSyncSock::ReadHandler, this,
                                         placeholders::error,
                                         placeholders::bytes_transferred));
}

bool KSyncSock::ValidateAndEnqueue(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    if (nlh->nlmsg_type == NLMSG_ERROR) {
        LOG(ERROR, "Ignoring Netlink error for seqno " << nlh->nlmsg_seq 
                        << " len " << nlh->nlmsg_len);
        assert(0);
        return true;
    }
    IoContext ioc;
    ioc.SetSeqno(nlh->nlmsg_seq);
    Tree::iterator it;
    {
        tbb::mutex::scoped_lock lock(mutex_);
        it = wait_tree_.find(ioc);
        if (it == wait_tree_.end()) {
            LOG(ERROR, "Netlink : Unknown seqno " << nlh->nlmsg_seq 
                            << " msgtype " << nlh->nlmsg_type);
            assert(0);
            return true;
        }
    }

    if (nlh->nlmsg_len > kBufLen) {
        LOG(ERROR, "Length of " << nlh->nlmsg_len << " is more than expected "
            "length of " << kBufLen);
        assert(0);
        return true;
    }

    IoContext *context = it.operator->();

    IoContext::IoContextWorkQId q_id = context->GetWorkQId();
    work_queue_[q_id]->Enqueue(data);
    return true;
}

// Process kernel data - executes in the task specified by IoContext
// Currently only Agent::KSync and Agent::Uve are possibilities
bool KSyncSock::ProcessKernelData(char *data) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    IoContext ioc;
    ioc.SetSeqno(nlh->nlmsg_seq);
    Tree::iterator it;
    {
        tbb::mutex::scoped_lock lock(mutex_);
        it = wait_tree_.find(ioc);
    }
    assert (it != wait_tree_.end());
    IoContext *context = it.operator->();

    AgentSandeshContext *ctxt = context->GetSandeshContext();
    ctxt->SetErrno(0);
    Decoder(data, ctxt);
    if (ctxt->GetErrno() != 0) {
        context->ErrorHandler(ctxt->GetErrno());
    }

    if (!(nlh->nlmsg_flags & NLM_F_MULTI) || (nlh->nlmsg_type == NLMSG_DONE)) {
        context->Handler();
        tbb::mutex::scoped_lock lock(mutex_);
        wait_tree_.erase(it);
        delete(context);
    }

    delete[] data;
    return true;
}
    
void KSyncSock::Decoder(char *data, SandeshContext *ctxt) {
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
                    decode_len = Sandesh::ReceiveBinaryMsgOne(decode_buf, 
                                                              decode_buf_len,
                                                              &err, ctxt);
                    if (decode_len < 0) {
                        LOG(DEBUG, "Incorrect decode len " << decode_len);
                        break;
                    }
                    decode_buf += decode_len;
                    decode_buf_len -= decode_len;
                }
            } else {
                LOG(ERROR, "Unkown generic netlink TLV type : " << attr->nla_type);
                assert(0);
            }
        } else {
            LOG(ERROR, "Unkown generic netlink cmd : " << genlh->cmd);
            assert(0);
        }
    } else if (nlh->nlmsg_type != NLMSG_DONE) {
        LOG(ERROR, "Netlink unkown message type : " << nlh->nlmsg_type);
        assert(0);
    }
    
}

// Write handler registered with boost::asio
void KSyncSock::WriteHandler(const boost::system::error_code& error,
                             size_t bytes_transferred) {
    if (error) {
        LOG(ERROR, "Netlink sock write error : " <<
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

void KSyncSock::BlockingSend(const char *msg, int msg_len) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)msg;
    nlh->nlmsg_pid = KSyncSock::GetPid();
    sock_type_->SendTo(buffer(msg, msg_len));
    return;
}

bool KSyncSock::BlockingRecv() {
    char *data = new char[kBufLen];
    struct nlmsghdr *nlh = (struct nlmsghdr *)data;
    bool ret = false;

    do {
        sock_type_->Receive(boost::asio::buffer(data, kBufLen));

        if (nlh->nlmsg_type == NLMSG_ERROR) {
            LOG(ERROR, "Netlink error for seqno " << nlh->nlmsg_seq 
                << " len " << nlh->nlmsg_len);
            return true;
        }

        AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext();
        ctxt->SetErrno(0);
        Decoder(data, ctxt);
        if (ctxt->GetErrno() != 0) {
            KSYNC_ERROR(VRouterError, "VRouter operation failed. Error <", 
                        ctxt->GetErrno(), ":", strerror(ctxt->GetErrno()), 
                        ">. Object <", "N/A", ">. State <", "N/A",
                        ">. Message number :", nlh->nlmsg_seq);
            ret = true;
        }
    } while ((nlh->nlmsg_flags & NLM_F_MULTI) && 
             (nlh->nlmsg_type != NLMSG_DONE));

    delete [] data;
    return ret;
}

void KSyncSock::SendAsync(KSyncEntry *entry, int msg_len, char *msg,
                          KSyncEntry::KSyncEvent event) {
    uint32_t seq = seqno_++;
    KSyncIoContext *ioc = new KSyncIoContext(entry, msg_len, msg, seq, event);
    SendAsyncImpl(msg_len, msg, ioc);
}

void KSyncSock::GenericSend(int msg_len, char *msg, 
                            IoContext *ioc) {
    SendAsyncImpl(msg_len, msg, ioc);
}

void IoContext::UpdateNetlinkHeader() {
    struct nlmsghdr *nlh = (struct nlmsghdr *)msg_;
    nlh->nlmsg_pid = KSyncSock::GetPid();
    nlh->nlmsg_seq = seqno_;
}

void KSyncSock::SendAsyncImpl(int msg_len, char *msg, 
                              IoContext *ioc) {
    ioc->UpdateNetlinkHeader();
    {
        tbb::mutex::scoped_lock lock(mutex_);
        wait_tree_.insert(*ioc);
    }

    sock_type_->AsyncSendTo(buffer(msg, msg_len),
                            boost::bind(&KSyncSock::WriteHandler, this,
                                        placeholders::error,
                                        placeholders::bytes_transferred));
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
    KSYNC_ERROR(VRouterError, "VRouter operation failed. Error <", err, 
                ":", strerror(err), ">. Object <", entry_->ToString(), 
                ">. State <", entry_->StateString(), ">. Message number :", 
                GetSeqno());
}
