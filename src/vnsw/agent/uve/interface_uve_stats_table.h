/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_uve_stats_table_h
#define vnsw_agent_interface_uve_stats_table_h

#include <uve/interface_uve_table.h>
#include <uve/l4_port_bitmap.h>
#include <pkt/flow_proto.h>
#include <pkt/flow_table.h>
#include <uve/stats_manager.h>

class InterfaceUveStatsTable : public InterfaceUveTable {
public:
    InterfaceUveStatsTable(Agent *agent, uint32_t default_intvl);
    virtual ~InterfaceUveStatsTable();
    void UpdateBitmap(const VmEntry* vm, uint8_t proto, uint16_t sport,
                      uint16_t dport);
    void SendInterfaceStats(void);
    void UpdateFloatingIpStats(const FipInfo &fip_info);
    InterfaceUveTable::FloatingIp * FipEntry
    (uint32_t fip, const string &vn, Interface *intf);
    void UpdatePortBitmap
    (const string &name, uint8_t proto, uint16_t sport, uint16_t dport);
    void IncrInterfaceAceStats(const std::string &itf, const std::string &u);
    void SendInterfaceAceStats(const string &name, UveInterfaceEntry *entry);

private:
    void SendInterfaceStatsMsg(UveInterfaceEntry* entry);
    uint64_t GetVmPortBandwidth
        (StatsManager::InterfaceStats *s, bool dir_in) const;
    bool FrameFipStatsMsg(const VmInterface *vm_intf,
                          vector<VmFloatingIPStats> &fip_list,
                          vector<VmFloatingIPStats> &diff_list,
                          bool &diff_list_send) const;
    bool FrameInterfaceStatsMsg(UveInterfaceEntry* entry,
                                UveVMInterfaceAgent *uve) const;

    DISALLOW_COPY_AND_ASSIGN(InterfaceUveStatsTable);
};

#endif // vnsw_agent_interface_uve_stats_table_h
