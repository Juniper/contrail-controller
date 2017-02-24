/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uve/vm_stat_kvm.h>
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

VmStatKvm::VmStatKvm(Agent *agent, const uuid &vm_uuid)
    : VmStat(agent, vm_uuid) {
}

VmStatKvm::~VmStatKvm() {
}

void VmStatKvm::ReadCpuStat() {
    std::string tmp;
    //Typical output from command
    //Id:             1
    //Name:           instance-00000001
    //UUID:           90cb7351-d2dc-4d8d-a216-2f460be183b6
    //OS Type:        hvm
    //State:          running
    //CPU(s):         1
    //CPU time:       13.4s
    //Max memory:     2097152 KiB

    //Get 'CPU time' from the output
    double cpu_stat = 0;
    std::string cpu_stat_str, state_str, cpu_count_str;

    while (data_ >> tmp) {
        if (tmp == "State:") {
            data_ >> state_str;
        }
        if (tmp == "CPU(s):") {
            data_ >> cpu_count_str;
        }
        if (tmp == "time:") {
            data_ >> cpu_stat_str;
            /* We expect 'State' and 'CPU(s)' fields to be present before
             * 'CPU time' field. So break from the loop when we are done with
             * reading of 'CPU time' field. */
            break;
        }
    }

    vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_UNKNOWN;
    if (state_str.size()) {
        if (state_str == "running") {
            vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_ACTIVE;
        } else if (state_str == "paused") {
            vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_PAUSED;
        } else if (state_str == "shut") {
            vm_state_ = VrouterAgentVmState::VROUTER_AGENT_VM_SHUTDOWN;
        }
    }
    vm_cpu_count_ = kInvalidCpuCount;
    if (cpu_count_str.size()) {
        stringToInteger(cpu_count_str, vm_cpu_count_);
    }
    //Remove the last character from 'cpu_stat_str'
    if (cpu_stat_str.size() >= 2) {
        cpu_stat_str.erase(cpu_stat_str.size() - 1);
        //Convert string to double
        stringstream ss(cpu_stat_str);
        ss >> cpu_stat;
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

    //Trigger a request to start vcpu stat collection
    GetVcpuStat();
}

void VmStatKvm::ReadVcpuStat() {
    std::string tmp;
    uint32_t index = 0;
    std::vector<double> vcpu_usage;
    //Read latest VCPU usage time
    while(data_ >> tmp) {
        if (tmp == "VCPU:") {
            //Current VCPU index
            data_ >> index;
        }

        if (tmp == "time:") {
            double usage = 0;
            data_ >> usage;
            vcpu_usage.push_back(usage);
        }
    }

    vcpu_usage_percent_.clear();
    if (prev_vcpu_usage_.size() != vcpu_usage.size()) {
        //In case a new VCPU get added
        prev_vcpu_usage_ = vcpu_usage;
    }

    time_t now;
    time(&now);
    //Calculate VCPU usage
    if (prev_vcpu_snapshot_time_) {
        for (uint32_t i = 0; i < vcpu_usage.size(); i++) {
            double cpu_usage = (vcpu_usage[i] - prev_vcpu_usage_[i])/
                               difftime(now, prev_vcpu_snapshot_time_);
            cpu_usage *= 100;
            vcpu_usage_percent_.push_back(cpu_usage);
        }
    }

    prev_vcpu_usage_ = vcpu_usage;
    prev_vcpu_snapshot_time_ = now;

    data_.str(" ");
    data_.clear();
    //Trigger a request to start mem stat
    GetMemStat();
}

void VmStatKvm::ReadMemStat() {
    if (pid_) {
        std::ostringstream proc_file;
        proc_file << "/proc/"<<pid_<<"/status";
        std::ifstream file(proc_file.str().c_str());

        bool vmsize = false;
        bool peak = false;
        bool rss = false;
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
    GetDiskName();
}

void VmStatKvm::GetCpuStat() {
    std::ostringstream cmd;
    cmd << "virsh dominfo " << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStatKvm::ReadCpuStat, this));
}

void VmStatKvm::GetVcpuStat() {
    std::ostringstream cmd;
    cmd << "virsh vcpuinfo " << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStatKvm::ReadVcpuStat, this));
}

void VmStatKvm::GetMemStat() {
    ReadMemStat();
}

void VmStatKvm::GetDiskName() {
    std::ostringstream cmd;
    cmd << "virsh domblklist " << agent_->GetUuidStr(vm_uuid_) << " | grep "
        << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStatKvm::ReadDiskName, this));
}

void VmStatKvm::ReadDiskName() {
    data_ >> disk_name_;
    if (!disk_name_.empty()) {
        GetDiskStat();
    } else {
        SendVmCpuStats();
        StartTimer();
    }
}

void VmStatKvm::GetDiskStat() {
    std::ostringstream cmd;
    cmd << "virsh domblkinfo " << agent_->GetUuidStr(vm_uuid_) << " "
        << disk_name_;
    ExecCmd(cmd.str(), boost::bind(&VmStatKvm::ReadDiskStat, this));
}

void VmStatKvm::ReadDiskStat() {
    bool disk_size_found = false, virtual_size_found = false;
    std::string tmp;
    std::string virtual_size_str, disk_size_str;
    while (data_ >> tmp) {
        if (tmp == "Capacity:") {
            data_ >> virtual_size_str;
            virtual_size_found = true;
        } else if (tmp == "Allocation:") {
            data_ >> disk_size_str;
            disk_size_found = true;
        }
        if (virtual_size_found && disk_size_found) {
            break;
        }
    }
    if (virtual_size_str.size() >= 2) {
        //Convert string to uint32_t
        stringstream ss(virtual_size_str);
        ss >> virtual_size_;
    }

    if (disk_size_str.size() >= 2) {
        //Convert string to uint32_t
        stringstream ss(disk_size_str);
        ss >> disk_size_;
    }

    SendVmCpuStats();
    StartTimer();
}

void VmStatKvm::ReadMemoryQuota() {
    std::string tmp;
    while (data_ >> tmp) {
        if (tmp == "actual") {
            data_ >> vm_memory_quota_;
        }
    }
    GetCpuStat();
}

void VmStatKvm::GetMemoryQuota() {
    std::ostringstream cmd;
    cmd << "virsh dommemstat " << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStatKvm::ReadMemoryQuota, this));
}

bool VmStatKvm::TimerExpiry() {
    if (pid_ == 0) {
        GetPid();
    } else {
        //Get CPU and memory stats
        GetCpuStat();
    }
    return false;
}

void VmStatKvm::ReadPid() {
    std::string tmp;
    uint32_t pid;

    while (data_) {
        data_ >> pid;
        data_ >> tmp;
        if (tmp.find("qemu") != std::string::npos ||
               tmp.find("kvm") != std::string::npos) {
            //Copy PID
            pid_ = pid;
            break;
        }
        //Flush out this line
        data_.ignore(512, '\n');
    }

    data_.str(" ");
    data_.clear();
    if (pid_) {
        //Successfully read pid of process, collect other data
        GetMemoryQuota();
    } else {
        retry_++;
        //Retry after timeout
        if (retry_ < kRetryCount) {
            StartTimer();
        }
    }
}

void VmStatKvm::GetPid() {
    std::ostringstream cmd;
    cmd << "ps -eo pid,cmd | grep " << agent_->GetUuidStr(vm_uuid_)
        << " | grep instance-";
    ExecCmd(cmd.str(), boost::bind(&VmStatKvm::ReadPid, this));
}

void VmStatKvm::Start() {
    GetPid();
}
