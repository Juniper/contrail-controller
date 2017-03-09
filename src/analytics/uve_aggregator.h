
/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

//
// uve_aggregator.h
// 
// This file has the interface for aggregating UVEs
//

#ifndef __UVE_AGGREGATOR__
#define __UVE_AGGREGATOR__

#include <memory>
#include <string>
#include <map>
#include <tbb/mutex.h>

namespace RdKafka {
    class Message;
}

class UVEAggregator {
public:
    static const uint64_t kCommitPeriod_us = 3600000000;

    typedef boost::function<RdKafka::ErrorCode (RdKafka::Message *message)> commitCb;
    UVEAggregator(const std::string& proxy, const std::string& conf,
            commitCb commit_cb, uint64_t partitions):
            topic_(proxy),
            commit_cb_(commit_cb),
            offsets_(partitions,0),
            part_period_(partitions,0),
            part_commit_msg_(partitions,
                    boost::shared_ptr<RdKafka::Message>()) {
        string residual(conf);
        while (true) {
            size_t pos = residual.find(":");
            conf_.insert(residual.substr(0,pos));
            if (pos!=string::npos) {
                residual = residual.substr(pos+1, string::npos);
            } else {
                break;
            }
        }
    }
    void Update(std::auto_ptr<RdKafka::Message> message,
                uint64_t ts=0);
    uint32_t Clear(const std::string& proxy, int partition);
private:
    const std::string topic_;
    std::set<std::string> conf_; 
    const commitCb commit_cb_;
    std::vector<uint64_t> offsets_;
    std::vector<uint64_t> part_period_;
    std::vector<boost::shared_ptr<RdKafka::Message> > part_commit_msg_;
    tbb::mutex mutex_;
};
#endif


