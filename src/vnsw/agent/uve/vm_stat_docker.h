/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_stat_docker_h
#define vnsw_agent_vm_stat_docker_h

#include "vm_stat.h"

class VmStatDocker : public VmStat {
public:
    VmStatDocker(Agent *agent, const boost::uuids::uuid &vm_uuid);
    ~VmStatDocker();
    static const long kOneSecInNanoSecs = 1000000000;

    void Start();
private:
    void ReadContainerId();
    void GetContainerId();
    void ReadCpuStat();
    void GetCpuStat();
    void ReadMemoryQuota();
    void GetMemoryQuota();
    void ReadMemStat();
    void GetMemStat();
    bool TimerExpiry();
    void GetPid();
    void ReadPid();

    std::string container_id_;

    DISALLOW_COPY_AND_ASSIGN(VmStatDocker);
};
#endif // vnsw_agent_vm_stat_docker_h
