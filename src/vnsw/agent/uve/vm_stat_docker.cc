/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uve/vm_stat_docker.h>
#include <uve/vm_stat_data.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <net/address.h>
#include <ifmap/ifmap_agent_parser.h>
#include <cmn/agent.h>
#include <uve/vrouter_uve_entry.h>
#include <sstream>
#include <fstream>
#include <uve/agent_uve.h>
#include <uve/vm_uve_table.h>

using namespace boost::uuids;
using namespace boost::asio;

VmStatDocker::VmStatDocker(Agent *agent, const uuid &vm_uuid)
    : VmStat(agent, vm_uuid), container_id_() {
}

VmStatDocker::~VmStatDocker() {
}

void VmStatDocker::Start() {
    if (vm_uuid_ == nil_uuid()) {
        return;
    }
    GetContainerId();
}

void VmStatDocker::ReadCpuStat() {
    double cpu_stat = 0;
    std::string cpu_stat_str;
    data_ >> cpu_stat_str;

    if (!cpu_stat_str.empty()) {
        //Convert string to double
        stringstream ss(cpu_stat_str);
        ss >> cpu_stat;
        //Convert from Nanoseconds to seconds
        cpu_stat = cpu_stat / kOneSecInNanoSecs;
    }

    time_t now;
    time(&now);
    if (prev_cpu_snapshot_time_) {
        cpu_usage_ = (cpu_stat - prev_cpu_stat_)/
                     difftime(now, prev_cpu_snapshot_time_);
        cpu_usage_ *= 100;
    }

    prev_cpu_stat_ = cpu_stat;
    prev_cpu_snapshot_time_ = now;

    //Clear buffer
    data_.str(" ");
    data_.clear();

    //Trigger a request to start Memory stat collection
    GetMemoryQuota();
}

void VmStatDocker::ReadMemStat() {
    if (pid_) {
        std::ostringstream proc_file;
        proc_file << "/proc/"<<pid_<<"/status";
        std::ifstream file(proc_file.str().c_str());

        bool vmsize = false;
        bool rss = false;
        bool peak = false;
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("VmSize") != std::string::npos) {
                std::stringstream vm(line);
                std::string tmp; vm >> tmp; vm >> virt_memory_;
                vmsize = true;
            }
            if (line.find("VmRSS") != std::string::npos) {
                std::stringstream vm(line);
                std::string tmp;
                vm >> tmp;
                vm >> mem_usage_;
                rss = true;
            }
            if (line.find("VmPeak") != std::string::npos) {
                std::stringstream vm(line);
                std::string tmp; vm >> tmp; vm >> virt_memory_peak_;
                peak = true;
            }
            if (rss && vmsize && peak)
                break;
        }
    }

    data_.str(" ");
    data_.clear();

    SendVmCpuStats();
    StartTimer();
}

void VmStatDocker::GetCpuStat() {
    std::ostringstream cmd;
    cmd << "cat /sys/fs/cgroup/cpuacct/docker/" << container_id_
        << "/cpuacct.usage";
    ExecCmd(cmd.str(), boost::bind(&VmStatDocker::ReadCpuStat, this));
}

void VmStatDocker::GetMemoryQuota() {
    std::ostringstream cmd;
    cmd << "cat /sys/fs/cgroup/memory/docker/" << container_id_
        << "/memory.stat";
    ExecCmd(cmd.str(), boost::bind(&VmStatDocker::ReadMemoryQuota, this));
}

void VmStatDocker::GetMemStat() {
    ReadMemStat();
}

void VmStatDocker::GetPid() {
    std::ostringstream cmd;
    cmd << "docker inspect " << container_id_ << "| grep -A3 \\\"Paused\\\":";
    ExecCmd(cmd.str(), boost::bind(&VmStatDocker::ReadPid, this));
}

void VmStatDocker::ReadPid() {
    /* Expecting data_ to have the following content
     *     "Paused": false,
     *     "Pid": 24430,
     *     "Restarting": false,
     *     "Running": true,
     */
    string tmp, paused_str, pid_str, running_str;
    while (data_ >> tmp) {
        if (tmp.find("Paused") != std::string::npos) {
            data_ >> paused_str;
        }
        if (tmp.find("Pid") != std::string::npos) {
            data_ >> pid_str;
        }
        if (tmp.find("Running") != std::string::npos) {
            data_ >> running_str;
            break;
        }
    }

    //Remove the last character from 'pid_str'
    if (pid_str.size() >= 2) {
        pid_str.erase(pid_str.size() - 1);
        //Convert string to uint32_t
        stringstream ss(pid_str);
        ss >> pid_;
    }

    vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_UNKNOWN;
    if (paused_str == "true,") {
        vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_PAUSED;
    } else if (running_str == "false,") {
        vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_SHUTDOWN;
    } else if (running_str == "true,") {
        vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_ACTIVE;
    }
    data_.str(" ");
    data_.clear();
    GetMemStat();
}

void VmStatDocker::ReadMemoryQuota() {
    std::string tmp;
    while (data_ >> tmp) {
        if (tmp == "hierarchical_memory_limit") {
            data_ >> vm_memory_quota_;
            /* Convert the 'vm_memory_quota_' to KiB to make it consistent with
               Kvm's 'vm_memory_quota_' */
            vm_memory_quota_/= 1024;
            break;
        }
    }

    data_.str(" ");
    data_.clear();
    GetPid();
}

bool VmStatDocker::TimerExpiry() {
    if (container_id_.empty()) {
        GetContainerId();
    } else {
        //Get CPU and memory stats
        GetCpuStat();
    }
    return false;
}

void VmStatDocker::ReadContainerId() {
    data_ >> container_id_;
    //Clear buffer
    data_.str(" ");
    data_.clear();

    if (!container_id_.empty()) {
        //Successfully read container-id, collect other data
        GetCpuStat();
    } else {
        retry_++;
        //Retry after timeout
        if (retry_ < kRetryCount) {
            StartTimer();
        }
    }
}

void VmStatDocker::GetContainerId() {
    std::ostringstream cmd;
    cmd << "docker ps --no-trunc | grep " << agent_->GetUuidStr(vm_uuid_)
        << " | awk '{ print $1 }'";
    ExecCmd(cmd.str(), boost::bind(&VmStatDocker::ReadContainerId, this));
}
