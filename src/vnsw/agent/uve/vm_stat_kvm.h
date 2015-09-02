/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_stat_kvm_h
#define vnsw_agent_vm_stat_kvm_h

#include "vm_stat.h"

class VmStatKvm : public VmStat {
public:
    VmStatKvm(Agent *agent, const boost::uuids::uuid &vm_uuid);
    ~VmStatKvm();

    void Start();
private:
    void ReadCpuStat();
    void ReadVcpuStat();
    void ReadMemStat();
    void ReadDiskStat();
    void ReadDiskName();
    void GetCpuStat();
    void GetVcpuStat();
    void GetMemStat();
    void GetDiskName();
    void GetDiskStat();
    bool TimerExpiry();
    void GetPid();
    void ReadPid();
    void ReadMemoryQuota();
    void GetMemoryQuota();

    DISALLOW_COPY_AND_ASSIGN(VmStatKvm);
};
#endif // vnsw_agent_vm_stat_kvm_h
