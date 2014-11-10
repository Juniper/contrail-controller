/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_entry_h
#define vnsw_agent_vn_uve_entry_h

#include <uve/l4_port_bitmap.h>
#include "pkt/flow_proto.h"
#include "pkt/flow_table.h"
#include <uve/vn_uve_entry_base.h>

//The class that defines data-structures to store VirtualNetwork information
//required for sending VirtualNetwork UVE.
class VnUveEntry : public VnUveEntryBase {
public:
    struct VnStats {
        VnStats(std::string vn, uint64_t bytes, uint64_t pkts, bool out) :
            prev_in_pkts_(0), prev_in_bytes_(0), prev_out_pkts_(0),
            prev_out_bytes_(0) {
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
        ~VnStats() {}
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
    typedef boost::shared_ptr<VnStats> VnStatsPtr;

    class VnStatsCmp {
        public:
            bool operator()(const VnStatsPtr &lhs, const VnStatsPtr &rhs) const {
                if (lhs.get()->dst_vn_.compare(rhs.get()->dst_vn_) < 0)
                    return true;
                return false;
            }

    };

    typedef std::set<VnStatsPtr, VnStatsCmp> VnStatsSet;

    VnUveEntry(Agent *agent, const VnEntry *vn);
    VnUveEntry(Agent *agent);
    virtual ~VnUveEntry();

    void UpdatePortBitmap(uint8_t proto, uint16_t sport, uint16_t dport);
    bool FillVrfStats(int vrf_id, UveVirtualNetworkAgent &s_vn);
    bool PopulateInterVnStats(UveVirtualNetworkAgent &s_vn);
    bool FrameVnStatsMsg(const VnEntry *vn, UveVirtualNetworkAgent &uve,
                         bool only_vrf_stats);
    void UpdateInterVnStats(const string &dst_vn, uint64_t bytes,
                            uint64_t pkts, bool outgoing);
    void ClearInterVnStats();
protected:
    L4PortBitmap port_bitmap_;
    VnStatsSet inter_vn_stats_;

private:
    bool SetVnPortBitmap(UveVirtualNetworkAgent &uve);
    bool UveVnInterfaceInStatsChanged(uint64_t bytes, uint64_t pkts) const;
    bool UveVnInterfaceOutStatsChanged(uint64_t bytes, uint64_t pkts) const;
    bool UveVnInBandChanged(uint64_t out_band) const;
    bool UveVnOutBandChanged(uint64_t out_band) const;
    bool UpdateVnFlowCount(const VnEntry *vn, UveVirtualNetworkAgent &s_vn);
    bool UpdateVnFipCount(int count, UveVirtualNetworkAgent &s_vn);
    bool UveVnFipCountChanged(int32_t size) const;
    bool UveInterVnInStatsChanged(const std::vector<UveInterVnStats>
                                  &new_list) const;
    bool UveInterVnOutStatsChanged(const std::vector<UveInterVnStats>
                                   &new_list) const;
    bool UveVnVrfStatsChanged(const std::vector<UveVrfStats> &vlist) const;
    bool UpdateVrfStats(const VnEntry *vn, UveVirtualNetworkAgent &s_vn);
    bool UveVnInFlowCountChanged(uint32_t size);
    bool UveVnOutFlowCountChanged(uint32_t size);

    tbb::mutex mutex_;
    uint64_t prev_stats_update_time_;
    uint64_t prev_in_bytes_;
    uint64_t prev_out_bytes_;
    DISALLOW_COPY_AND_ASSIGN(VnUveEntry);
};

#endif // vnsw_agent_vn_uve_entry_h
