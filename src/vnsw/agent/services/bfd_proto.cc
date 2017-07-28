/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <oper/metadata_ip.h>
#include <services/bfd_proto.h>
#include <services/bfd_handler.h>
#include <services/services_types.h>
#include <services/services_init.h>

BfdProto::BfdProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::BFD, io),
    msg_(new PktInfo(agent, BFD_TX_BUFF_LEN, PktHandler::BFD, 0)),
    communicator_(this),
    server_(new BFD::Server(agent->event_manager(), &communicator_)),
    client_(new BFD::Client(&communicator_)), handler_(agent, msg_, io) {

    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    agent->health_check_table()->RegisterHealthCheckCallback(
        boost::bind(&BfdProto::BfdHealthCheckSessionControl, this, _1, _2));
}

BfdProto::~BfdProto() {
}

ProtoHandler *BfdProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                          boost::asio::io_service &io) {
    return new BfdHandler(agent(), info, io);
}

bool BfdProto::BfdHealthCheckSessionControl(
               HealthCheckTable::HealthCheckServiceAction action,
               HealthCheckInstanceService *service) {

    IpAddress service_ip = service->ip()->service_ip();
    BFD::SessionKey key(service->ip()->destination_ip(),
                        BFD::SessionIndex(service->interface()->id()),
                        BFD::kSingleHop,
                        service_ip);

    tbb::mutex::scoped_lock lock(mutex_);
    switch (action) {
        case HealthCheckTable::CREATE_SERVICE:
        case HealthCheckTable::UPDATE_SERVICE:
            {
                if (service_ip == Ip4Address(METADATA_IP_ADDR)) {
                    return false;
                }

                uint32_t tx_interval = service->service()->delay() * 1000000 +
                                       service->service()->delay_usecs();
                if (!tx_interval) {
                    tx_interval = kMinTxInterval;
                }
                uint32_t rx_interval = service->service()->timeout() * 1000000 +
                                       service->service()->timeout_usecs();
                if (!rx_interval) {
                    rx_interval = kMinRxInterval;
                }
                uint32_t multiplier = service->service()->max_retries();
                if (!multiplier) {
                    multiplier = kMultiplier;
                }

                BFD::SessionConfig session_config;
                session_config.desiredMinTxInterval =
                    boost::posix_time::microseconds(tx_interval);
                session_config.requiredMinRxInterval =
                    boost::posix_time::microseconds(rx_interval);
                session_config.detectionTimeMultiplier = multiplier;

                client_->AddSession(key, session_config);
                sessions_.insert(SessionsPair(
                          service->interface()->id(), service));
                BFD_TRACE(Trace, "Add / Update",
                          service->ip()->destination_ip().to_string(),
                          service_ip.to_string(), service->interface()->id(),
                          tx_interval, rx_interval, multiplier);
                break;
            }

        case HealthCheckTable::DELETE_SERVICE:
            client_->DeleteSession(key);
            sessions_.erase(service->interface()->id());
            BFD_TRACE(Trace, "Delete",
                      service->ip()->destination_ip().to_string(),
                      service_ip.to_string(), service->interface()->id(),
                      0, 0, 0);
            break;

        case HealthCheckTable::RUN_SERVICE:
            break;

        case HealthCheckTable::STOP_SERVICE:
            break;

        default:
            assert(0);
    }

    return true;
}

bool BfdProto::GetServiceAddress(uint32_t interface, IpAddress &address) {
    tbb::mutex::scoped_lock lock(mutex_);
    Sessions::iterator it = sessions_.find(interface);
    if (it == sessions_.end()) {
        return false;
    }
    address = it->second->ip()->service_ip();
    return true;
}

void BfdProto::NotifyHealthCheckInstanceService(uint32_t interface,
                                                std::string &data) {
    tbb::mutex::scoped_lock lock(mutex_);
    Sessions::iterator it = sessions_.find(interface);
    if (it == sessions_.end()) {
        return;
    }
    it->second->OnRead(data);
}

void BfdProto::BfdCommunicator::SendPacket(
         const boost::asio::ip::udp::endpoint &local_endpoint,
         const boost::asio::ip::udp::endpoint &remote_endpoint,
         const BFD::SessionIndex &session_index,
         const boost::asio::mutable_buffer &packet, int pktSize) {
    bfd_proto_->handler_.SendPacket(local_endpoint, remote_endpoint,
                                    session_index.if_index, packet, pktSize);
    bfd_proto_->IncrementSent();
}

void BfdProto::BfdCommunicator::NotifyStateChange(const BFD::SessionKey &key,
                                                  const bool &up) {
    std::string data = up ? "success" : "failure";
    bfd_proto_->NotifyHealthCheckInstanceService(key.index.if_index, data);
}
