/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_uve_entry_h
#define vnsw_agent_vrouter_uve_entry_h

#include <uve/vrouter_uve_entry_base.h>
#include <uve/stats_manager.h>

//The class that defines data-structures to store VRouter information
//required for sending VRouter UVE.
class VrouterUveEntry : public VrouterUveEntryBase {
public:
    typedef std::map<string, uint64_t> DerivedStatsMap;
    typedef std::pair<string, uint64_t> DerivedStatsPair;

    VrouterUveEntry(Agent *agent);
    virtual ~VrouterUveEntry();
    L4PortBitmap port_bitmap() { return port_bitmap_; }

    virtual bool SendVrouterMsg();
    void UpdateBitmap(uint8_t proto, uint16_t sport, uint16_t dport);

protected:
    uint8_t bandwidth_count_;
    L4PortBitmap port_bitmap_;
    uint64_t prev_flow_setup_rate_export_time_;
    uint64_t prev_flow_created_;
    uint64_t prev_flow_aged_;
private:
    void InitPrevStats() const;
    void FetchDropStats(DerivedStatsMap &ds) const;
    bool SetVrouterPortBitmap(VrouterStatsAgent &vr_stats);
    uint64_t CalculateBandwitdh(uint64_t bytes, int speed_mbps,
                               int diff_seconds, double *utilization_bps) const;
    uint64_t GetBandwidthUsage(StatsManager::InterfaceStats *s,
                              bool dir_in, int mins, double *util) const;
    bool BuildPhysicalInterfaceBandwidth(std::vector<AgentIfBandwidth> &list,
                                         uint8_t mins) const;
    bool BuildPhysicalInterfaceBandwidth(map<string,uint64_t> &imp,
                                         map<string,uint64_t> &omp, 
                                         uint8_t mins, double &in_util,
                                         double &out_util) const;
    bool BuildPhysicalInterfaceList(std::vector<AgentIfStats> &list) const;
    std::string GetMacAddress(const MacAddress &mac) const;
    void BuildXmppStatsList(std::vector<AgentXmppStats> &list) const;

    uint64_t start_time_;
    DISALLOW_COPY_AND_ASSIGN(VrouterUveEntry);
};

#endif // vnsw_agent_vrouter_uve_entry_h
