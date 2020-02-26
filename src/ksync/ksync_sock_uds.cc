/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "ksync_sock.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <errno.h>

class AgentParam;

using namespace boost::asio;

/////////////////////////////////////////////////////////////////////////////
// KSyncSockUds routines
/////////////////////////////////////////////////////////////////////////////
//Unix domain socket class for interacting with dpdk vrouter

class KSyncSockUdsReadTask : public Task {
public:
    KSyncSockUdsReadTask(TaskScheduler *scheduler, KSyncSockUds *queue) :
        Task(scheduler->GetTaskId("Ksync::KSyncSockUdsRead"), 0), queue_(queue) {
    }
    ~KSyncSockUdsReadTask() {
    }

    bool Run() {
        queue_->Run();
        return true;
    }
    std::string Description() const { return "KSyncSockUdsRead"; }
private:
    KSyncSockUds *queue_;

};



string KSyncSockUds::sockpath_= KSYNC_AGENT_VROUTER_SOCK_PATH;

KSyncSockUds::KSyncSockUds(boost::asio::io_service &ios) :
    sock_(ios),
    server_ep_(sockpath_),
    rx_buff_(NULL),
    rx_buff_q_(NULL),
    remain_(0),
    socket_(0),
    connected_(false) {
    boost::system::error_code ec;
retry:;
    sock_.connect(server_ep_, ec);
    if (ec) {
        sleep(1);
        goto retry;
    }
    socket_ = sock_.native();
    connected_ = true;
    rx_buff_   = new char[10*kBufLen];
    rx_buff_q_ = new char[10*kBufLen];
}

bool KSyncSockUds::Run() {
    AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext(0);
    char *ret_buff = new char[1024*kBufLen];
    boost::system::error_code ec;

    // Read data from the socket and append it to the existing
    // unprocessed data in the local buffer.
    while (1) {
        char *bufp = rx_buff_;
        struct nlmsghdr *nlh = NULL;
        struct nlmsghdr tnlh;
        size_t offset = 0;
        int ret_val;
        size_t bytes_transferred = 0;
        bytes_transferred = ret_val = recv(socket_, rx_buff_, 10*kBufLen, 0);
        if (ret_val == 0) {
            // connection reset by peer
            // close socket and exit
            sock_.close(ec);
            LOG(INFO, " dpdk vrouter is down, exiting.. errno:" << errno);
            exit(0);
        }
        if (ret_val < 0) {
            if (errno != EAGAIN) {
                sock_.close(ec);
                connected_ = false;
retry:;
                sock_.connect(server_ep_, ec);
                if (ec) {
                    sleep(1);
                    goto retry;
                }
                socket_ = sock_.native();
                connected_ = true;
            }
            continue;
        }

        if (remain_ != 0) {
            if (remain_ < sizeof(struct nlmsghdr)) {
                memcpy((char *)&tnlh, rx_buff_q_, remain_);
                memcpy(((char *)&tnlh) + remain_, rx_buff_,
                        (sizeof(struct nlmsghdr) - remain_));
                nlh =  &tnlh;
            } else {
                nlh =  (struct nlmsghdr *)rx_buff_q_;
            }
            if (remain_ > nlh->nlmsg_len)
                assert(0);
            memcpy(ret_buff, rx_buff_q_, remain_);
            memcpy(ret_buff+remain_, rx_buff_, nlh->nlmsg_len - remain_);
            bufp += (nlh->nlmsg_len - remain_);
            ctxt->SetErrno(0);
            ProcessDataInline(ret_buff);
            offset = nlh->nlmsg_len - remain_;
        }
        while (offset < bytes_transferred) {
            if ((bytes_transferred - offset) > (sizeof(struct nlmsghdr))) {
                nlh =  (struct nlmsghdr *)(rx_buff_ + offset);
                if ((bytes_transferred - offset) > nlh->nlmsg_len) {
                    memcpy(ret_buff, rx_buff_ + offset, nlh->nlmsg_len);
                    ctxt->SetErrno(0);
                    ProcessDataInline(ret_buff);
                    offset += nlh->nlmsg_len;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        memcpy(rx_buff_q_, rx_buff_ + offset, bytes_transferred - offset);
        remain_ = bytes_transferred - offset;
    }
    return true;
}

void KSyncSockUds::Init(io_service &ios, const std::string &cpu_pin_policy,
    const std::string &sockpathvr) {
    KSyncSock::SetSockTableEntry(new KSyncSockUds(ios));
    SetNetlinkFamilyId(10);
    KSyncSock::Init(false, cpu_pin_policy);
    sockpath_ = sockpathvr;
}

uint32_t KSyncSockUds::GetSeqno(char *data) {
    return GetNetlinkSeqno(data);
}

bool KSyncSockUds::IsMoreData(char *data) {
    return NetlinkMsgDone(data);
}

bool KSyncSockUds::Decoder(char *data, AgentSandeshContext *context) {
    KSyncSockNetlink::NetlinkDecoder(data, context);
    return true;
}

bool KSyncSockUds::BulkDecoder(char *data,
                               KSyncBulkSandeshContext *bulk_sandesh_context) {
    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetNetlinkPayload(data, &buf, &buf_len);
    return bulk_sandesh_context->Decoder(buf, buf_len, NLA_ALIGNTO, IsMoreData(data));
}

void KSyncSockUds::AsyncSendTo(KSyncBufferList *iovec, uint32_t seq_no,
                               HandlerCb cb) {
    if (connected_ == true)
        SendTo(iovec, seq_no);
}

size_t KSyncSockUds::SendTo(KSyncBufferList *iovec, uint32_t seq_no) {
    size_t len = 0, ret;
    struct msghdr msg;
    struct iovec iov[max_bulk_msg_count_*2];
    int i;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;

    ResetNetlink(nl_client_);
    int offset = nl_client_->cl_buf_offset;
    UpdateNetlink(nl_client_, bulk_buf_size_, seq_no);

    KSyncBufferList::iterator it = iovec->begin();
    iovec->insert(it, buffer((char *)nl_client_->cl_buf, offset));

    int count = iovec->size();
    for(i = 0; i < count; i++) {
        mutable_buffers_1 buf = iovec->at(i);
        size_t buf_size = boost::asio::buffer_size(buf);
        void* cbuf = boost::asio::buffer_cast<void*>(buf);
        len += buf_size;
        iov[i].iov_base = cbuf;
        iov[i].iov_len = buf_size;
    }

    msg.msg_iovlen = i;
    ret = sendmsg(socket_, &msg, 0);
    if (ret != len) {
        LOG(ERROR, "sendmsg failure " << ret << "len " << len);
    }
    return len;
}

bool KSyncSockUds::Validate(char *data) {
    return true;
}

void KSyncSockUds::AsyncReceive(mutable_buffers_1 buf, HandlerCb cb) {
    static int started = 0;
    if (!started) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        //Receive is handled in a separate Run() thread
        KSyncSockUdsReadTask *task = new KSyncSockUdsReadTask(scheduler, this);
        scheduler->Enqueue(task);
        started = 1;
    }
}

void KSyncSockUds::Receive(mutable_buffers_1 buf) {
    boost::system::error_code ec;
    uint32_t bytes_read = 0;
    const struct nlmsghdr *nlh = NULL;
    int ret_val = 0;

    char *netlink_header(buffer_cast<char *>(buf));

    while (bytes_read < sizeof(struct nlmsghdr)) {
        char *buffer = netlink_header + bytes_read;
        ret_val = recv(socket_, buffer, sizeof(struct nlmsghdr) - bytes_read, 0);
        if (ret_val == 0) {
            // connection reset by peer
            // close socket and exit
            sock_.close(ec);
            LOG(INFO, " dpdk vrouter is down, exiting.. errno:" << errno);
            exit(0);
        }
        if (ret_val < 0) {
            if (errno != EAGAIN) {
                sock_.close(ec);
                connected_ = false;
retry_1:;
                sock_.connect(server_ep_, ec);
                if (ec) {
                    sleep(1);
                    goto retry_1;
                }
                socket_ = sock_.native();
                connected_ = true;
            }
            continue;
        }
        bytes_read += ret_val;
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
    char *data(buffer_cast<char *>(buf + sizeof(struct nlmsghdr)));

    while (bytes_read < payload_size) {
        char *buffer = data + bytes_read;
        ret_val = recv(socket_, buffer, payload_size - bytes_read, 0);
        if (ret_val == 0) {
            // connection reset by peer
            // close socket and exit
            sock_.close(ec);
            LOG(INFO, " dpdk vrouter is down, exiting.. errno:" << errno);
            exit(0);
        }
        if (ret_val < 0) {
            if (errno != EAGAIN) {
                sock_.close(ec);
                connected_ = false;
retry_2:;
                sock_.connect(server_ep_, ec);
                if (ec) {
                    sleep(1);
                    goto retry_2;
                }
                socket_ = sock_.native();
                connected_ = true;
            }
            continue;
        }
        bytes_read += ret_val;
    }
}
