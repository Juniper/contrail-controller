/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "inter_vn_stats.h"
#include <oper/interface.h>
#include <oper/mirror_table.h>

using namespace std;

InterVnStatsCollector::VnStatsSet *InterVnStatsCollector::Find(string vn) {
     VnStatsMap::iterator it = inter_vn_stats_.find(vn);

     if (it != inter_vn_stats_.end()) {
         return it->second;
     }
     return NULL;
}

void InterVnStatsCollector::PrintAll() {
    VnStatsMap::iterator it = inter_vn_stats_.begin();
    while(it != inter_vn_stats_.end()) {
        PrintVn(it->first);
        it++;
    }
}

void InterVnStatsCollector::PrintVn(string vn) {

    VnStatsSet *stats_set;
    VnStats *stats;

    LOG(DEBUG, "...........Stats for Vn " << vn);
    VnStatsMap::iterator it = inter_vn_stats_.find(vn);
    if (it != inter_vn_stats_.end()) {
        stats_set = it->second;

        /* Remove all the elements of map entry value which is a set */
        VnStatsSet::iterator stats_it = stats_set->begin();
        while(stats_it != stats_set->end()) {
            stats = *stats_it;
            stats_it++;
            LOG(DEBUG, "    Other-VN " << stats->dst_vn_);
            LOG(DEBUG, "        in_pkts " << stats->in_pkts_ << " in_bytes " << stats->in_bytes_);
            LOG(DEBUG, "        out_pkts " << stats->out_pkts_ << " out_bytes " << stats->out_bytes_);
        }
    }
}

void InterVnStatsCollector::Remove(string vn) {
        
    VnStatsSet *stats_set;
    VnStats *stats;

    VnStatsMap::iterator it = inter_vn_stats_.find(vn);
    if (it != inter_vn_stats_.end()) {
        stats_set = it->second;
        /* Remove the entry from the inter_vn_stats_ map */
        inter_vn_stats_.erase(it);

        /* Remove all the elements of map entry value which is a set */
        VnStatsSet::iterator stats_it = stats_set->begin();
        VnStatsSet::iterator del_it;
        while(stats_it != stats_set->end()) {
            stats = *stats_it;
            delete stats;
            del_it = stats_it;
            stats_it++;
            stats_set->erase(del_it);
        }
        delete stats_set;
    }
}

void InterVnStatsCollector::UpdateVnStats(FlowEntry *fe, uint64_t bytes, 
                                          uint64_t pkts) {
    string src_vn = fe->data.source_vn, dst_vn = fe->data.dest_vn;

    if (!fe->data.source_vn.length())
        src_vn = *FlowHandler::UnknownVn();
    if (!fe->data.dest_vn.length())
        dst_vn = *FlowHandler::UnknownVn();

    /* When packet is going from src_vn to dst_vn it should be interpreted 
     * as ingress to vrouter and hence in-stats for src_vn w.r.t. dst_vn
     * should be incremented. Similarly when the packet is egressing vrouter 
     * it should be considered as out-stats for dst_vn w.r.t. src_vn.
     * Here the direction "in" and "out" should be interpreted w.r.t vrouter
     */
    if (fe->local_flow) {
        VnStatsUpdateInternal(src_vn, dst_vn, bytes, pkts, false);
        VnStatsUpdateInternal(dst_vn, src_vn, bytes, pkts, true);
    } else {
        if (fe->data.ingress) {
            VnStatsUpdateInternal(src_vn, dst_vn, bytes, pkts, false);
        } else {
            VnStatsUpdateInternal(dst_vn, src_vn, bytes, pkts, true);
        }
    }
    //PrintAll();
}

void InterVnStatsCollector::VnStatsUpdateInternal(string src_vn, string dst_vn,
                                                  uint64_t bytes, uint64_t pkts, 
                                                  bool outgoing) {
    VnStatsSet *stats_set;
    VnStats *stats;
    
    tbb::mutex::scoped_lock lock(mutex_);
    VnStatsMap::iterator it = inter_vn_stats_.find(src_vn);

    if (it == inter_vn_stats_.end()) {
       stats = new VnStats(dst_vn, bytes, pkts, outgoing);
       stats_set = new VnStatsSet;
       stats_set->insert(stats);
       inter_vn_stats_.insert(make_pair(src_vn, stats_set));
    } else {
       stats_set = it->second;
       VnStats key(dst_vn, 0, 0, false);
       VnStatsSet::iterator stats_it = stats_set->find(&key);
       if (stats_it == stats_set->end()) {
           stats = new VnStats(dst_vn, bytes, pkts, outgoing);
           stats_set->insert(stats);
       } else {
           stats = *stats_it;
           if (outgoing) {
               stats->out_bytes_ += bytes;
               stats->out_pkts_ += pkts;
           } else {
               stats->in_bytes_ += bytes;
               stats->in_pkts_ += pkts;
           }
       }

    }
}
