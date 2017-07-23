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

class BfdProto : public Proto {
public:
    class BfdCommunicator : public BFD::Connection {
    public:
        BfdCommunicator(BfdProto *bfd_proto) :
            bfd_proto_(bfd_proto), server_(NULL) {}
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
        server_.DeleteClientSessions();
        sessions_.clear();
    }

    bool BfdSessionControl(HealthCheckTable::HealthCheckServiceAction action,
                           HealthCheckInstanceService *service);
    HealthCheckInstanceService *
        FindHealthCheckInstanceService(uint32_t interface);
    BfdCommunicator &bfd_communicator() { return communicator_; }

private:
    friend BfdCommunicator;
    // map from interface id to health check instance service
    typedef std::map<uint32_t, HealthCheckInstanceService *> Sessions;
    typedef std::pair<uint32_t, HealthCheckInstanceService *> SessionsPair;

    tbb::mutex mutex_; // lock for sessions_ access between health check & BFD
    boost::shared_ptr<PktInfo> msg_;
    BfdCommunicator communicator_;
    BFD::Server server_;
    BFD::Client client_;
    BfdHandler handler_;
    Sessions sessions_;

    DISALLOW_COPY_AND_ASSIGN(BfdProto);
};

#endif // vnsw_agent_bfd_proto_h_
