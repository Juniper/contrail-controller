/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __SFLOW_GENERATOR_H__
#define __SFLOW_GENERATOR_H__

#include "base/queue_task.h"

#include "db_handler.h"

class SFlowCollector;

struct SFlowQueueEntry {
    SFlowQueueEntry(boost::asio::const_buffer buf, size_t len,
                    uint64_t timestamp, SFlowCollector* collector);
    ~SFlowQueueEntry();

    boost::asio::const_buffer buffer;
    size_t length;
    uint64_t timestamp;
    SFlowCollector* const sflow_collector;
};

class SFlowGenerator {
public:
    explicit SFlowGenerator(const std::string& ip_address, 
                            SFlowCollector* sflow_collector,
                            DbHandler* db_handler);
    ~SFlowGenerator();
    bool EnqueueSFlowPacket(boost::asio::const_buffer& buffer,
                            size_t length, uint64_t timestamp);
private:
    bool ProcessSFlowPacket(boost::shared_ptr<SFlowQueueEntry>);

    typedef WorkQueue<boost::shared_ptr<SFlowQueueEntry> > SFlowPktQueue;
    
    std::string ip_address_;
    SFlowCollector* const sflow_collector_;
    DbHandler* const db_handler_;
    SFlowPktQueue sflow_pkt_queue_;
    uint64_t num_packets_;
    uint64_t num_invalid_packets_;
    uint64_t time_first_pkt_seen_;
    uint64_t time_last_pkt_seen_;

    DISALLOW_COPY_AND_ASSIGN(SFlowGenerator);
};

#endif // __SFLOW_GENERATOR_H__
