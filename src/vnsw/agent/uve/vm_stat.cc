/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uve/vm_stat.h>
#include <uve/vm_stat_data.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <base/address.h>
#include <ifmap/ifmap_agent_parser.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <uve/vrouter_uve_entry.h>
#include <sstream>
#include <fstream>
#include <uve/agent_uve.h>
#include <uve/vm_uve_table.h>

using namespace boost::uuids;
using namespace boost::asio;

VmStat::VmStat(Agent *agent, const uuid &vm_uuid):
    agent_(agent), vm_uuid_(vm_uuid), mem_usage_(0),
    virt_memory_(0), virt_memory_peak_(0), vm_memory_quota_(0),
    prev_cpu_stat_(0), cpu_usage_(0),
    prev_cpu_snapshot_time_(0), prev_vcpu_snapshot_time_(0),
    input_(*(agent_->event_manager()->io_service())),
    timer_(TimerManager::CreateTimer(*(agent_->event_manager())->io_service(),
    "VmStatTimer")), marked_delete_(false), pid_(0), retry_(0), virtual_size_(0),
    disk_size_(0), disk_name_(),
    vm_state_(VrouterAgentVmState::VROUTER_AGENT_VM_UNKNOWN),
    prev_vm_state_(VrouterAgentVmState::VROUTER_AGENT_VM_UNKNOWN),
    vm_cpu_count_(kInvalidCpuCount), prev_vm_cpu_count_(kInvalidCpuCount) {
}

VmStat::~VmStat() {
    TimerManager::DeleteTimer(timer_);
}

void VmStat::ReadData(const boost::system::error_code &ec,
                      size_t read_bytes, DoneCb &cb) {
    if (read_bytes) {
        data_<< rx_buff_;
    }

    if (ec) {
        boost::system::error_code close_ec;
        input_.close(close_ec);
        call_back_ = cb;
        //Enqueue a request to process data
        VmStatData *vm_stat_data = new VmStatData(this);

        VmUveTable *vmt = static_cast<VmUveTable *>
            (agent_->uve()->vm_uve_table());
        vmt->EnqueueVmStatData(vm_stat_data);
    } else {
        bzero(rx_buff_, sizeof(rx_buff_));
        async_read(
            input_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&VmStat::ReadData, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, cb));
    }
}

void VmStat::ProcessData() {
    if (!call_back_.empty())
        call_back_();
}

void VmStat::ExecCmd(std::string cmd, DoneCb cb) {
    char *argv[4];
    char shell[80] = "/bin/sh";
    char option[80] = "-c";
    char ccmd[256];
    strncpy(ccmd, cmd.c_str(), sizeof(ccmd));

    argv[0] = shell;
    argv[1] = option;
    argv[2] = ccmd;
    argv[3] = 0;

    int out[2];
    if (pipe(out) < 0) {
        return;
    }

    if (vfork() == 0) {
        //Close read end of pipe
        close(out[0]);
        dup2(out[1], STDOUT_FILENO);
        //Close out[1] as stdout is a exact replica of out[1]
        close(out[1]);

        /* Close all the open fds before execvp */
        CloseTaskFds();
        execvp(argv[0], argv);
        perror("execvp");
        exit(127);
    }

    //Close write end of pipe
    close(out[1]);

    boost::system::error_code ec;
    int fd = ::dup(out[0]);
    close(out[0]);
    if (fd == -1) {
        return;
    }
    input_.assign(fd, ec);
    if (ec) {
        close(fd);
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    async_read(
        input_, boost::asio::buffer(rx_buff_, kBufLen),
        boost::bind(&VmStat::ReadData, this, boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred, cb));
}

bool VmStat::BuildVmStatsMsg(VirtualMachineStats *uve) {
    uve->set_name(UuidToString(vm_uuid_));

    std::vector<VmCpuStats> cpu_stats_list;
    VmCpuStats stats;
    stats.set_cpu_one_min_avg(cpu_usage_);
    stats.set_vm_memory_quota(vm_memory_quota_);
    stats.set_rss(mem_usage_);
    stats.set_virt_memory(virt_memory_);
    stats.set_peak_virt_memory(virt_memory_peak_);
    stats.set_disk_allocated_bytes(virtual_size_);
    stats.set_disk_used_bytes(disk_size_);


    cpu_stats_list.push_back(stats);
    uve->set_cpu_stats(cpu_stats_list);

    return true;
}

bool VmStat::BuildVmMsg(UveVirtualMachineAgent *uve) {
    uve->set_name(UuidToString(vm_uuid_));

    VmCpuStats stats;
    stats.set_cpu_one_min_avg(cpu_usage_);
    stats.set_vm_memory_quota(vm_memory_quota_);
    stats.set_rss(mem_usage_);
    stats.set_virt_memory(virt_memory_);
    stats.set_peak_virt_memory(virt_memory_peak_);
    stats.set_disk_allocated_bytes(virtual_size_);
    stats.set_disk_used_bytes(disk_size_);

    uve->set_cpu_info(stats);

    vnsConstants vns;
    if (vm_state_ != VrouterAgentVmState::VROUTER_AGENT_VM_UNKNOWN) {
        if (vm_state_ != prev_vm_state_) {
            uve->set_vm_state(vns.VrouterAgentVmStateMap.at(vm_state_));
            prev_vm_state_ = vm_state_;
        }
    }

    if (vm_cpu_count_ != kInvalidCpuCount) {
        if (vm_cpu_count_ != prev_vm_cpu_count_) {
            uve->set_vm_cpu_count(vm_cpu_count_);
            prev_vm_cpu_count_ = vm_cpu_count_;
        }
    }

    return true;
}

void VmStat::SendVmCpuStats() {
    //We need to send same cpu info in two different UVEs
    //(VirtualMachineStats and UveVirtualMachineAgent). One of them uses
    //stats-oracle infra and other one does not use it. We need two because
    //stats-oracle infra returns only SUM of cpu-info over a period of time
    //and current value is returned using non-stats-oracle version. Using
    //stats oracle infra we can still query the current value but for simpler
    //interface we are sending current value in separate UVE.
    //Also the non-stats oracle version has additional fields of vm_state and
    //vm_cpu_count which are not sent in stats-oracle version.
    VirtualMachineStats vm_agent;
    if (BuildVmStatsMsg(&vm_agent)) {
        VmUveTable *vmt = static_cast<VmUveTable *>
            (agent_->uve()->vm_uve_table());
        vmt->DispatchVmStatsMsg(vm_agent);
    }
    UveVirtualMachineAgent vm_msg;
    if (BuildVmMsg(&vm_msg)) {
        agent_->uve()->vm_uve_table()->DispatchVmMsg(vm_msg);
    }
}

bool VmStat::TimerExpiry() {
    return false;
}

void VmStat::StartTimer() {
    timer_->Cancel();
    timer_->Start(agent_->params()->vmi_vm_vn_uve_interval_msecs(),
                  boost::bind(&VmStat::TimerExpiry, this));
}

void VmStat::Start() {
}

void VmStat::Stop() {
    marked_delete_ = true;
    if (timer_->running() || retry_ == kRetryCount) {
        //If timer is fired, then we are in middle of
        //vm stat collection, in such case dont delete the vm stat
        //entry as asio may be using it
        delete this;
    }
}
