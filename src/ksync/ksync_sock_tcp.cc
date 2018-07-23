/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include "ksync_sock.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

using namespace boost::asio;

/////////////////////////////////////////////////////////////////////////////
// KSyncSockTcp routines
/////////////////////////////////////////////////////////////////////////////
//TCP socket class for interacting with vrouter
KSyncSockTcp::KSyncSockTcp(EventManager *evm,
    boost::asio::ip::address ip_address, int port) : TcpServer(evm), evm_(evm),
    session_(NULL), server_ep_(ip_address, port), connect_complete_(false) {

    reset_use_wait_tree();
    set_process_data_inline();
    if (rx_buff_ == NULL) {
        rx_buff_ = new char[kBufLen];
    }
    rx_buff_rem_ = new char[kBufLen];
    remain_ = 0;

    session_ = CreateSession();
    Connect(session_, server_ep_);
}

void KSyncSockTcp::Init(EventManager *evm, boost::asio::ip::address ip_addr,
                        int port, const std::string &cpu_pin_policy) {
    KSyncSock::SetSockTableEntry(new KSyncSockTcp(evm, ip_addr, port));
    SetNetlinkFamilyId(10);
    KSyncSock::Init(false, cpu_pin_policy);
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
    size_t len = 0, ret;
    struct msghdr msg;
    struct iovec iov[max_bulk_msg_count_*2];
    int i, fd;

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
    fd = tcp_socket_->native();
    ret = sendmsg(fd, &msg, 0);
    if (ret != len) {
        LOG(ERROR, "sendmsg failure " << ret << "len " << len);
    }
    return len;
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
                               KSyncBulkSandeshContext *bulk_sandesh_context) {
    // Get sandesh buffer and buffer-length
    uint32_t buf_len = 0;
    char *buf = NULL;
    GetNetlinkPayload(data, &buf, &buf_len);
    return bulk_sandesh_context->Decoder(buf, buf_len, NLA_ALIGNTO, IsMoreData(data));
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
    AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext(0);
    ctxt->SetErrno(0);
    ProcessDataInline((char *) msg);
    return true;
}

bool KSyncSockTcp::Run() {
    AgentSandeshContext *ctxt = KSyncSock::GetAgentSandeshContext(0);
    int fd = tcp_socket_->native();

    while (1) {
        char *bufp = rx_buff_;
        struct nlmsghdr *nlh = NULL;
        struct nlmsghdr tnlh;
        int offset = 0;
        int bytes_transferred = 0;

        bytes_transferred = recv(fd, rx_buff_, kBufLen, 0);
        if (bytes_transferred <= 0) {
             LOG(ERROR, "Connection to dpdk-vrouter lost.");
             sleep(10);
             exit(EXIT_FAILURE);
        }

        if (remain_ != 0) {
            if (remain_ < sizeof(struct nlmsghdr)) {
                memcpy((char *)&tnlh, rx_buff_rem_, remain_);
                memcpy(((char *)&tnlh) + remain_, rx_buff_,
                        (sizeof(struct nlmsghdr) - remain_));
                nlh =  &tnlh;
            } else {
                nlh =  (struct nlmsghdr *)rx_buff_rem_;
            }

            if (remain_ > nlh->nlmsg_len)
                assert(0);

            memcpy(rx_buff_rem_+remain_, rx_buff_, nlh->nlmsg_len - remain_);
            bufp += (nlh->nlmsg_len - remain_);
            ctxt->SetErrno(0);
            ProcessDataInline(rx_buff_rem_);
            offset = nlh->nlmsg_len - remain_;
        }

        while (offset < bytes_transferred) {
            if ((unsigned int)(bytes_transferred - offset) > (sizeof(struct nlmsghdr))) {
                nlh =  (struct nlmsghdr *)(rx_buff_ + offset);
                if ((unsigned int)(bytes_transferred - offset) > nlh->nlmsg_len) {
                    ctxt->SetErrno(0);
                    ProcessDataInline(rx_buff_ + offset);
                    offset += nlh->nlmsg_len;
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        remain_ = bytes_transferred - offset;
        if (remain_) {
            memcpy(rx_buff_rem_, rx_buff_ + offset, bytes_transferred - offset);
        }
    }

    return true;
}

class KSyncSockTcpReadTask : public Task {
public:
    KSyncSockTcpReadTask(TaskScheduler *scheduler, KSyncSockTcp *sock) :
        Task(scheduler->GetTaskId("Ksync::KSyncSockTcpRead"), 0), sock_(sock) {
    }
    ~KSyncSockTcpReadTask() {
    }

    bool Run() {
        sock_->Run();
        return true;
    }
    std::string Description() const { return "KSyncSockTcpRead"; }
private:
    KSyncSockTcp *sock_;

};

void KSyncSockTcp::AsyncReadStart() {
    static int started = 0;
    boost::system::error_code ec;

    if (!started) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        KSyncSockTcpReadTask *task = new KSyncSockTcpReadTask(scheduler, this);
        tcp_socket_->non_blocking(false, ec);
        scheduler->Enqueue(task);
        started = 1;
    }
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
        tcp_socket_ = session_->socket();
        connect_complete_ = true;
        session_->SetTcpNoDelay();
        session_->SetTcpSendBufSize(max_bulk_buf_size_*16);
        session_->SetTcpRecvBufSize(max_bulk_buf_size_*16);
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

KSyncSockTcpSession::~KSyncSockTcpSession() {
    if (reader_) {
        delete reader_;
    }
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
