/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __src_vnsw_agent_vrouter_ksync_ksync_flow_memory_h
#define __src_vnsw_agent_vrouter_ksync_ksync_flow_memory_h

/*
 * Module responsible to manage the VRouter memory mapped to agent
 */
#include <list>
#include <base/address.h>
#include <vrouter/ksync/ksync_memory.h>

class Timer;
class KSync;
struct FlowKey;
struct vr_flow_entry;
struct vr_flow_stats;
struct KFlowData;

class KSyncFlowMemory : public KSyncMemory {
public:
    KSyncFlowMemory(KSync *ksync, uint32_t minor_id);
    virtual ~KSyncFlowMemory() {}

    virtual void Init();

    static void VrFlowToIp(const vr_flow_entry *kflow, IpAddress *sip,
                           IpAddress *dip);
    const vr_flow_entry *GetKernelFlowEntry(uint32_t idx,
                                            bool ignore_active_status) const;
    const vr_flow_entry *GetValidKFlowEntry(const FlowKey &key,
                                            uint32_t idx, uint8_t gen_id) const;
    const vr_flow_entry *GetKFlowStats(const FlowKey &key, uint32_t idx,
                                       uint8_t gen_id,
                                       vr_flow_stats *stats) const;
    const vr_flow_entry *GetKFlowStatsAndInfo(const FlowKey &key,
                                              uint32_t idx,
                                              uint8_t gen_id,
                                              vr_flow_stats *stats,
                                              KFlowData *info) const;
    bool GetFlowKey(uint32_t index, FlowKey *key, bool *is_nat_flow);

    bool IsEvictionMarked(const vr_flow_entry *entry, uint16_t flags) const;

    virtual int get_entry_size();
    virtual bool IsInactiveEntry(uint32_t idx, uint8_t &gen_id);
    virtual void SetTableSize();
    virtual int EncodeReq(nl_client *nl, uint32_t attr_len);
    virtual void CreateProtoAuditEntry(uint32_t index, uint8_t gen_id);
    virtual void InitTest();
    virtual void Shutdown();
    void DecrementHoldFlowCounter();
    void IncrementHoldFlowCounter();
    void UpdateAgentHoldFlowCounter();

private:
    uint32_t hold_flow_counter_;
    void KFlow2FlowKey(const vr_flow_entry *entry, FlowKey *key) const;
    void ReadFlowInfo(const vr_flow_entry *k_flow, vr_flow_stats *stats,
                      KFlowData *info) const;
    const vr_flow_entry           *flow_table_;
};

#endif //  __src_vnsw_agent_vrouter_ksync_ksync_flow_memory_h
