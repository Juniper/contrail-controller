/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bfd_proto_h_
#define vnsw_agent_bfd_proto_h_

#include "pkt/proto.h"
#include "services/bfd_handler.h"
#include "oper/health_check.h"

#include "bfd/bfd_client.h"
#include "bfd/bfd_server.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_session.h"

#include "base/test/task_test_util.h"

#define BFD_TX_BUFF_LEN 128

#define BFD_TRACE(obj, ...)                                                 \
do {                                                                        \
    Bfd##obj::TraceMsg(BfdTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);     \
} while (false)

class BfdProto : public Proto {
public:
    static const uint32_t kMultiplier = 2;
    static const uint32_t kMinRxInterval = 500000; // microseconds
    static const uint32_t kMinTxInterval = 500000; // microseconds

    struct BfdStats {
        BfdStats() { Reset(); }
        void Reset() { bfd_sent = bfd_received = 0; }

        uint32_t bfd_sent;
        uint32_t bfd_received;
    };

    class BfdCommunicator : public BFD::Connection {
    public:
        BfdCommunicator(BfdProto *bfd_proto) :
            bfd_proto_(bfd_proto), server_(NULL) {}
        virtual ~BfdCommunicator() {}
        virtual void SendPacket(
                const boost::asio::ip::udp::endpoint &local_endpoint,
                const boost::asio::ip::udp::endpoint &remote_endpoint,
                const BFD::SessionIndex &session_index,
                const boost::asio::mutable_buffer &packet, int pktSize);
        virtual void NotifyStateChange(const BFD::SessionKey &key, const bool &up);
        virtual BFD::Server *GetServer() const { return server_; }
        virtual void SetServer(BFD::Server *server) { server_ = server; }

    private:
        BfdProto *bfd_proto_;
        BFD::Server *server_;
    };

    BfdProto(Agent *agent, boost::asio::io_service &io);
    virtual ~BfdProto();
    ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io);
    void Shutdown() {
        delete client_;
        client_ = NULL;

        // server_->DeleteClientSessions();
        // TASK_UTIL_EXPECT_TRUE(server_->event_queue()->IsQueueEmpty());
        server_->event_queue()->Shutdown();
        delete server_;
        server_ = NULL;

        sessions_.clear();
    }

    bool BfdHealthCheckSessionControl(
               HealthCheckTable::HealthCheckServiceAction action,
               HealthCheckInstanceService *service);
    bool GetSourceAddress(uint32_t interface, IpAddress &address);
    void NotifyHealthCheckInstanceService(uint32_t interface, std::string &data);
    BfdCommunicator &bfd_communicator() { return communicator_; }

    void IncrementSent() { stats_.bfd_sent++; }
    void IncrementReceived() { stats_.bfd_received++; }
    const BfdStats &GetStats() const { return stats_; }
    uint32_t ActiveSessions() const { return sessions_.size(); }

private:
    friend BfdCommunicator;
    // map from interface id to health check instance service
    typedef std::map<uint32_t, HealthCheckInstanceService *> Sessions;
    typedef std::pair<uint32_t, HealthCheckInstanceService *> SessionsPair;

    tbb::mutex mutex_; // lock for sessions_ access between health check & BFD
    boost::shared_ptr<PktInfo> msg_;
    BfdCommunicator communicator_;
    BFD::Server *server_;
    BFD::Client *client_;
    BfdHandler handler_;
    Sessions sessions_;
    BfdStats stats_;

    DISALLOW_COPY_AND_ASSIGN(BfdProto);
};

#endif // vnsw_agent_bfd_proto_h_
