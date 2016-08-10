/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SFLOW_COLLECTOR_H__
#define __SFLOW_COLLECTOR_H__

#include <boost/ptr_container/ptr_map.hpp>

#include "io/udp_server.h"
#include "base/timer.h"

#include "db_handler.h"

class SFlowGenerator; 

class SFlowCollector : public UdpServer {
public:
    explicit SFlowCollector(EventManager* evm,
                            DbHandlerPtr db_handler,
                            const std::string& ip_address, int port);
    ~SFlowCollector();
    virtual void Start();
    virtual void Shutdown();

private:
    void HandleReceive(const boost::asio::const_buffer& buffer,
                       boost::asio::ip::udp::endpoint remote_endpoint,
                       size_t bytes_transferred,
                       const boost::system::error_code& error);
    void ProcessSFlowPacket(const boost::asio::const_buffer& buffer,
                            size_t length,
                            const std::string& generator_ip);
    SFlowGenerator* GetSFlowGenerator(const std::string& generator_ip);

    typedef boost::ptr_map<std::string, SFlowGenerator> SFlowGeneratorMap;
   
    DbHandlerPtr db_handler_;
    std::string ip_address_;
    int port_;
    SFlowGeneratorMap generator_map_;
    uint64_t num_packets_;
    uint64_t time_first_pkt_seen_;
    uint64_t time_last_pkt_seen_;
    
    DISALLOW_COPY_AND_ASSIGN(SFlowCollector);
};

#endif // __SFLOW_COLLECTOR_H__
