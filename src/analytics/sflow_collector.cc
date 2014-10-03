/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "db_handler.h"
#include "sflow_collector.h"
#include "sflow_generator.h"

SFlowListener::SFlowListener(EventManager* evm)
    : UdpServer(evm) {
}

SFlowListener::~SFlowListener() {
}

void SFlowListener::Start(const std::string& ip_address, int port) {
    if (ip_address.empty()) {
        Initialize(port);
    } else {
        Initialize(ip_address, port);
    }
    StartReceive();
}

void SFlowListener::Shutdown() {
    UdpServer::Shutdown();
}

void SFlowListener::HandleReceive(boost::asio::const_buffer& buffer,
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

SFlowCollector::SFlowCollector(EventManager* evm,
            DbHandler* db_handler, int port, 
            int generator_inactive_timeout)
    : SFlowListener(evm),
      db_handler_(db_handler),
      ip_address_(),
      port_(port),
      generator_inactive_timeout_(generator_inactive_timeout),
      generator_cleanup_timer_(NULL) {
}

SFlowCollector::~SFlowCollector() {
}

void SFlowCollector::Start() {
    if (port_ != -1) {
        SFlowListener::Start(ip_address_, port_);
    }
}

void SFlowCollector::Shutdown() {
    SFlowListener::Shutdown();
}

void SFlowCollector::ProcessSFlowPacket(boost::asio::const_buffer& buffer,
                                        size_t length, 
                                        const std::string& generator_ip) {
   LOG(DEBUG, "Received sFlow packet from " << generator_ip);
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

void SFlowCollector::SFlowGeneratorCleanupHandler() {
}
