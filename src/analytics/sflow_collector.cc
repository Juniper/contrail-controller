/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "db_handler.h"
#include "sflow_collector.h"
#include "sflow_generator.h"

SFlowCollector::SFlowCollector(EventManager* evm,
            DbHandlerPtr db_handler,
            const std::string& ip_address, int port)
    : UdpServer(evm),
      db_handler_(db_handler),
      ip_address_(ip_address),
      port_(port) {
}

SFlowCollector::~SFlowCollector() {
}

void SFlowCollector::Start() {
    if (port_ != -1) {
        if (ip_address_.empty()) {
            Initialize(port_);
        } else {
            Initialize(ip_address_, port_);
        }
        StartReceive();
    }
}

void SFlowCollector::Shutdown() {
    if (port_ != -1) {
        UdpServer::Shutdown();
    }
}

void SFlowCollector::HandleReceive(const boost::asio::const_buffer& buffer,
            boost::asio::ip::udp::endpoint remote_endpoint,
            size_t bytes_transferred,
            const boost::system::error_code& error) {
    if (!error) {
        ProcessSFlowPacket(buffer, bytes_transferred, 
                           remote_endpoint.address().to_string());
    } else {
        DeallocateBuffer(buffer);
    }
}

void SFlowCollector::ProcessSFlowPacket(const boost::asio::const_buffer& buffer,
                                        size_t length, 
                                        const std::string& generator_ip) {
   num_packets_++;
   time_last_pkt_seen_ = UTCTimestampUsec();
   if (!time_first_pkt_seen_) {
       time_first_pkt_seen_ = time_last_pkt_seen_;
   }
   SFlowGenerator* sflow_gen = GetSFlowGenerator(generator_ip);
   sflow_gen->EnqueueSFlowPacket(buffer, length, time_last_pkt_seen_);
}

SFlowGenerator* SFlowCollector::GetSFlowGenerator(
                                    const std::string& generator_ip) {
    SFlowGeneratorMap::iterator it = generator_map_.find(generator_ip);
    if (it == generator_map_.end()) {
        it = generator_map_.insert(const_cast<std::string&>(generator_ip),
                new SFlowGenerator(generator_ip, this, db_handler_)).first;
    }
    return it->second;
}
