/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_VROUTER_KSYNC_KSYNC_BRIDGE_MEMORY_H_
#define SRC_VNSW_AGENT_VROUTER_KSYNC_KSYNC_BRIDGE_MEMORY_H_

class Timer;
class KSync;
struct vr_bridge_entry;
/*
 * Module responsible to manage the VRouter memory mapped to agent
 */
#include <list>
#include <base/address.h>
#include <vrouter/ksync/ksync_memory.h>

class KSyncBridgeMemory : public KSyncMemory {
public:
     KSyncBridgeMemory(KSync *ksync, uint32_t minor_id);
     virtual ~KSyncBridgeMemory();

     virtual void InitTest();
     virtual void Shutdown();
     virtual int get_entry_size();
     virtual bool IsInactiveEntry(uint32_t idx, uint8_t &gen_id);
     virtual void SetTableSize();
     virtual int EncodeReq(nl_client *nl, uint32_t attr_len);
     virtual void CreateProtoAuditEntry(uint32_t index, uint8_t gen_id);
     vr_bridge_entry* GetBridgeEntry(uint32_t idx);
private:
    vr_bridge_entry        *bridge_table_;
};
#endif
