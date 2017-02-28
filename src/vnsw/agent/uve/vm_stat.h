/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_stat_h
#define vnsw_agent_vm_stat_h

#include <vector>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/common/vns_types.h>
#include <virtual_machine_types.h>
#include <boost/uuid/uuid_io.hpp>
#include "base/timer.h"
#include <cmn/agent_cmn.h>

class VmStatData;

class VmStat {
public:
    static const size_t kBufLen = 4098;
    static const uint32_t kTimeout = 60 * 1000;
    static const uint32_t kRetryCount = 3;
    static const uint16_t kInvalidCpuCount = 0xFFFF;
    typedef boost::function<void(void)> DoneCb;

    VmStat(Agent *agent, const boost::uuids::uuid &vm_uuid);
    virtual ~VmStat();
    bool marked_delete() const { return marked_delete_; }

    virtual void Start();
    void Stop();
    void ProcessData();
private:
    bool BuildVmStatsMsg(VirtualMachineStats *uve);
    bool BuildVmMsg(UveVirtualMachineAgent *uve);
    void ReadData(const boost::system::error_code &ec, size_t read_bytes,
                  DoneCb &cb);
    virtual bool TimerExpiry();

protected:
    void StartTimer();
    void ExecCmd(std::string cmd, DoneCb cb);
    void SendVmCpuStats();

    Agent *agent_;
    const boost::uuids::uuid vm_uuid_;
    uint32_t mem_usage_;
    uint32_t virt_memory_;
    uint32_t virt_memory_peak_;
    uint32_t vm_memory_quota_;
    double   prev_cpu_stat_;
    double   cpu_usage_;
    time_t   prev_cpu_snapshot_time_;
    std::vector<double> prev_vcpu_usage_;
    std::vector<double> vcpu_usage_percent_;
    time_t   prev_vcpu_snapshot_time_;
    char     rx_buff_[kBufLen];
    std::stringstream data_;
    boost::asio::posix::stream_descriptor input_;
    Timer *timer_;
    bool marked_delete_;
    uint32_t pid_;
    uint32_t retry_;
    DoneCb call_back_;
    uint32_t virtual_size_;
    uint32_t disk_size_;
    std::string disk_name_;
    VrouterAgentVmState::type vm_state_;
    VrouterAgentVmState::type prev_vm_state_;
    uint16_t vm_cpu_count_;
    uint16_t prev_vm_cpu_count_;
    DISALLOW_COPY_AND_ASSIGN(VmStat);
};
#endif // vnsw_agent_vm_stat_h
