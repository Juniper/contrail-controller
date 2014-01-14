/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_stat_data_h
#define vnsw_agent_vm_stat_data_h

class VmStat;

class VmStatData {
public:
    VmStatData(VmStat *obj):
        vm_stat_(obj) {
    }

    VmStat *vm_stat() const { return vm_stat_; }
private:
    VmStat *vm_stat_;
    DISALLOW_COPY_AND_ASSIGN(VmStatData);
};
#endif // vnsw_agent_vm_stat_data_h
