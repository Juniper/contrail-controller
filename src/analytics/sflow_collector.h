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

class SFlowListener : public UdpServer {
public:
    explicit SFlowListener(EventManager* evm);
    virtual ~SFlowListener();
    virtual void Start(const std::string& ip_address, int port);
    virtual void Shutdown();
protected:
    virtual void ProcessSFlowPacket(boost::asio::const_buffer& buffer,
                                    size_t length,
                                    const std::string& generator_ip) = 0;
private:
    void HandleReceive(boost::asio::const_buffer& buffer,
                       boost::asio::ip::udp::endpoint remote_endpoint,
                       size_t bytes_transferred,
                       const boost::system::error_code& error);
    
    DISALLOW_COPY_AND_ASSIGN(SFlowListener);
};

class SFlowCollector : public SFlowListener {
public:
    explicit SFlowCollector(EventManager* evm, DbHandler* db_handler,
                            int port, int generator_inactive_timeout);
    ~SFlowCollector();
    virtual void Start();
    virtual void Shutdown();
protected:
    virtual void ProcessSFlowPacket(boost::asio::const_buffer& buffer,
                                    size_t length,
                                    const std::string& generator_ip);
private:
    SFlowGenerator* GetSFlowGenerator(const std::string& generator_ip);
    void SFlowGeneratorCleanupHandler();

    typedef boost::ptr_map<std::string, SFlowGenerator> SFlowGeneratorMap;
   
    DbHandler* const db_handler_;
    std::string ip_address_;
    int port_;
    int generator_inactive_timeout_;
    Timer* generator_cleanup_timer_;
    SFlowGeneratorMap generator_map_;
    uint64_t num_packets_;
    uint64_t time_first_pkt_seen_;
    uint64_t time_last_pkt_seen_;
    
    DISALLOW_COPY_AND_ASSIGN(SFlowCollector);
};

#endif // __SFLOW_COLLECTOR_H__
