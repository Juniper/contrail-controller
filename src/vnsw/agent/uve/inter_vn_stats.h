/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inter_vn_stats_h
#define vnsw_agent_inter_vn_stats_h

#include "pkt/pkt_flow.h"
#include "pkt/flowtable.h"
#include <set>
#include <map>

struct VnStats {
    std::string dst_vn;
    uint64_t in_pkts;
    uint64_t in_bytes;
    uint64_t out_pkts;
    uint64_t out_bytes;
    VnStats(std::string vn, uint64_t bytes, uint64_t pkts, bool out) {
        dst_vn = vn;
        if (out) {
            out_bytes = bytes;
            out_pkts = pkts;
            in_bytes = 0;
            in_pkts = 0;
        } else {
            in_bytes = bytes;
            in_pkts = pkts;
            out_bytes = 0;
            out_pkts = 0;
        }
    }
};

class VnStatsCmp {
public:
    bool operator()(const VnStats *lhs, const VnStats *rhs) const {
         if (lhs->dst_vn.compare(rhs->dst_vn) < 0)
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
private:
    VnStatsMap inter_vn_stats_;
    void VnStatsUpdateInternal(std::string src_vn, std::string dst_vn, uint64_t bytes, 
                               uint64_t pkts, bool outgoing);
    DISALLOW_COPY_AND_ASSIGN(InterVnStatsCollector);
};

#endif //vnsw_agent_inter_vn_stats_h

