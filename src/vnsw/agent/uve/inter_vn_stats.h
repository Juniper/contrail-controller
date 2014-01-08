/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inter_vn_stats_h
#define vnsw_agent_inter_vn_stats_h

#include "pkt/flow_proto.h"
#include "pkt/flow_table.h"
#include <set>
#include <map>
#include <tbb/mutex.h>

struct VnStats {
    VnStats(std::string vn, uint64_t bytes, uint64_t pkts, bool out) :
        prev_in_pkts_(0), prev_in_bytes_(0), prev_out_pkts_(0), prev_out_bytes_(0) {
        dst_vn_ = vn;
        if (out) {
            out_bytes_ = bytes;
            out_pkts_ = pkts;
            in_bytes_ = 0;
            in_pkts_ = 0;
        } else {
            in_bytes_ = bytes;
            in_pkts_ = pkts;
            out_bytes_ = 0;
            out_pkts_ = 0;
        }
    }
    std::string dst_vn_;
    uint64_t in_pkts_;
    uint64_t in_bytes_;
    uint64_t out_pkts_;
    uint64_t out_bytes_;
    uint64_t prev_in_pkts_;
    uint64_t prev_in_bytes_;
    uint64_t prev_out_pkts_;
    uint64_t prev_out_bytes_;
};

class VnStatsCmp {
public:
    bool operator()(const VnStats *lhs, const VnStats *rhs) const {
         if (lhs->dst_vn_.compare(rhs->dst_vn_) < 0)
             return true;
         return false;
    }

};

class InterVnStatsCollector {
public:
    typedef std::set<VnStats *, VnStatsCmp> VnStatsSet;
    typedef std::map<std::string, VnStatsSet *> VnStatsMap;
    InterVnStatsCollector() {};
    virtual ~InterVnStatsCollector() {
        assert(inter_vn_stats_.size() == 0);
    };
    void UpdateVnStats(FlowEntry *entry, uint64_t bytes, uint64_t pkts);
    VnStatsSet *Find(std::string vn);
    void Remove(std::string vn);
    void PrintAll();
    void PrintVn(std::string vn);
    tbb::mutex & mutex() { return mutex_; }
private:
    VnStatsMap inter_vn_stats_;
    tbb::mutex mutex_;
    void VnStatsUpdateInternal(std::string src_vn, std::string dst_vn, uint64_t bytes, 
                               uint64_t pkts, bool outgoing);
    DISALLOW_COPY_AND_ASSIGN(InterVnStatsCollector);
};

#endif //vnsw_agent_inter_vn_stats_h

