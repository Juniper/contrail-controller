/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_uve_entry_h
#define vnsw_agent_vrouter_uve_entry_h

#include <uve/vrouter_uve_entry_base.h>

//The class that defines data-structures to store VRouter information
//required for sending VRouter UVE.
class VrouterUveEntry : public VrouterUveEntryBase {
public:
    VrouterUveEntry(Agent *agent);
    virtual ~VrouterUveEntry();
    L4PortBitmap port_bitmap() { return port_bitmap_; }

    bool SendVrouterMsg();
    void UpdateBitmap(uint8_t proto, uint16_t sport, uint16_t dport);
    uint32_t GetCpuCount();

protected:
    VrouterStatsAgent prev_stats_;
    uint8_t bandwidth_count_;
    uint8_t cpu_stats_count_;
    L4PortBitmap port_bitmap_;
private:
    //The following Dispatch functions are not made const function because
    //in derived class they need to be non-const
    virtual void DispatchVrouterStatsMsg(const VrouterStatsAgent &uve);
    virtual void DispatchComputeCpuStateMsg(const ComputeCpuState &ccs);
    void InitPrevStats() const;
    void FetchDropStats(AgentDropStats &ds) const;
    bool SetVrouterPortBitmap(VrouterStatsAgent &vr_stats);
    uint8_t CalculateBandwitdh(uint64_t bytes, int speed_mbps,
                               int diff_seconds) const;
    uint8_t GetBandwidthUsage(AgentStatsCollector::InterfaceStats *s,
                              bool dir_in, int mins) const;
    bool BuildPhysicalInterfaceBandwidth(std::vector<AgentIfBandwidth> &list,
                                         uint8_t mins) const;
    bool BuildPhysicalInterfaceList(std::vector<AgentIfStats> &list) const;
    std::string GetMacAddress(const MacAddress &mac) const;
    void BuildXmppStatsList(std::vector<AgentXmppStats> &list) const;
    void BuildAndSendComputeCpuStateMsg(const CpuLoadInfo &info);

    uint64_t start_time_;
    DISALLOW_COPY_AND_ASSIGN(VrouterUveEntry);
};

#endif // vnsw_agent_vrouter_uve_entry_h
