/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_VROUTER_KSYNC_KSYNC_MEMORY_H_
#define SRC_VNSW_AGENT_VROUTER_KSYNC_KSYNC_MEMORY_H_

/*
 * Module responsible to manage the VRouter memory mapped to agent
 */
#include <list>
#include <base/address.h>
struct nl_client;
class KSync;
class KSyncMemory {
public:
    // Time to sweep flow-table for audit
    static const uint32_t kAuditSweepTime = 180;
    // Timer interval for audit process
    static const uint32_t kAuditYieldTimer = 100;  // in msec
    // Flows in HOLD state longer than kAuditTimeout are deleted
    static const uint32_t kAuditTimeout = (5 * 1000 * 1000); // in usec
    // Upper limit on number of entries to visit per timer
    static const uint32_t kAuditYieldMax = (1024);
    // Lower limit on number of entries to visit per timer
    static const uint32_t kAuditYieldMin = (100);

    KSyncMemory(KSync *ksync, uint32_t minor_id);
    virtual ~KSyncMemory();

    virtual void Init();
    void InitMem();
    virtual void InitTest();
    virtual void Shutdown();
    bool AuditProcess();
    void MapSharedMemory();
    void GetTableSize();
    int GetKernelTableSize();
    void UnmapMemTest();

    virtual int EncodeReq(nl_client *nl, uint32_t attr_len) = 0;
    virtual int get_entry_size() = 0;
    virtual void SetTableSize() {};
    virtual bool IsInactiveEntry(uint32_t idx, uint8_t &gen_id) = 0;
    virtual void CreateProtoAuditEntry(uint32_t index, uint8_t gen_id) = 0;
    virtual void DecrementHoldFlowCounter() {};
    virtual void IncrementHoldFlowCounter() {};
    virtual void UpdateAgentHoldFlowCounter() {};
    KSync* ksync() const { return ksync_;}
    void set_major_devid(int id) { major_devid_ = id;}
    void set_table_size(int count) { table_size_ = count; }
    void set_table_path(const std::string &path) {
        table_path_ = path;
    }
    uint32_t audit_timeout() const { return audit_timeout_; }
    void Mmap(bool unlink);
    uint32_t table_entries_count() { return table_entries_count_; }

protected:
    struct AuditEntry {
        AuditEntry(uint32_t flow_idx, uint8_t gen_id,
                   uint64_t t) : audit_idx(flow_idx),
                   audit_gen_id(gen_id), timeout(t) {}

        uint32_t audit_idx;
        uint8_t audit_gen_id;
        uint64_t timeout;
    };

    KSync        *ksync_;
    void         *table_;
    // Name of file used to map flow table
    std::string   table_path_;
    // major dev-id on linux based implementation
    int           major_devid_;
    int           minor_devid_;
    // Size of flow table memory
    int           table_size_;
    // Count of entries in flow-table
    uint32_t      table_entries_count_;
    // Audit related entries
    Timer                   *audit_timer_;

    uint32_t                audit_timeout_;
    uint32_t                audit_yield_;
    uint32_t                audit_interval_;
    uint32_t                audit_idx_;
    std::list<AuditEntry> audit_list_;
};
#endif
