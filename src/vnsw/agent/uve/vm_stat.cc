/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <uve/vm_stat.h>
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

using namespace boost::uuids;
using namespace boost::asio;

VmStat::VmStat(Agent *agent, const uuid &vm_uuid):
    agent_(agent), vm_uuid_(vm_uuid), mem_usage_(0), 
    virt_memory_(0), virt_memory_peak_(0), vm_memory_quota_(0), 
    prev_cpu_stat_(0), cpu_usage_(0), prev_cpu_snapshot_time_(0), 
    prev_vcpu_snapshot_time_(0), 
    input_(*(agent_->event_manager()->io_service())),
    timer_(TimerManager::CreateTimer(*(agent_->event_manager())->io_service(),
    "VmStatTimer")), marked_delete_(false), pid_(0), retry_(0) {
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

        agent_->uve()->vm_uve_table()->EnqueueVmStatData(vm_stat_data);
    } else {
        bzero(rx_buff_, sizeof(rx_buff_));
        async_read(input_, boost::asio::buffer(rx_buff_, kBufLen),
                   boost::bind(&VmStat::ReadData, this, placeholders::error,
                   placeholders::bytes_transferred, cb));
    }
}

void VmStat::ProcessData() {
    call_back_();
}

void VmStat::ExecCmd(std::string cmd, DoneCb cb) {
    char *argv[4];
    char shell[80] = "/bin/sh";
    char option[80] = "-c";
    char ccmd[80];
    strncpy(ccmd, cmd.c_str(), 80);

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
        execvp(argv[0], argv);
        perror("execvp");
        exit(127);
    }

    //Close write end of pipe
    close(out[1]);

    boost::system::error_code ec;
    input_.assign(::dup(out[0]), ec);
    close(out[0]);
    if (ec) {
        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    async_read(input_, boost::asio::buffer(rx_buff_, kBufLen),
               boost::bind(&VmStat::ReadData, this, placeholders::error,
                           placeholders::bytes_transferred, cb));
}

void VmStat::ReadCpuStat() {
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
    std::string cpu_stat_str;
    while (data_ >> tmp) {
        if (tmp == "time:") {
            data_ >> cpu_stat_str;
            break;
        }
    }

    uint32_t num_of_cpu = agent_->uve()->vrouter_uve_entry()->GetCpuCount();
    if (num_of_cpu == 0) {
        GetVcpuStat();
        return;
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
        cpu_usage_ /= num_of_cpu;
    }

    prev_cpu_stat_ = cpu_stat;
    prev_cpu_snapshot_time_ = now;

    //Clear buffer
    data_.str(" ");
    data_.clear();

    //Trigger a request to start vcpu stat collection
    GetVcpuStat();
}

void VmStat::ReadVcpuStat() {
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

void VmStat::ReadMemStat() {
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
    //Send Stats
    //We need to send same cpu info in two different UVEs
    //(VirtualMachineStats and UveVirtualMachineAgent). One of them uses
    //stats-oracle infra and other one does not use it. We need two because
    //stats-oracle infra returns only SUM of cpu-info over a period of time
    //and current value is returned using non-stats-oracle version. Using
    //stats oracle infra we can still query the current value but it is not
    //simple and hence we sending current value in separate UVE.
    VirtualMachineStats vm_agent;
    if (BuildVmStatsMsg(&vm_agent)) {
        agent_->uve()->vm_uve_table()->DispatchVmStatsMsg(vm_agent);
    }
    UveVirtualMachineAgent vm_msg;
    if (BuildVmMsg(&vm_msg)) {
        agent_->uve()->vm_uve_table()->DispatchVmMsg(vm_msg);
    }
    StartTimer();    
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

    uve->set_cpu_info(stats);

    return true;
}

void VmStat::GetCpuStat() {
    std::ostringstream cmd;
    cmd << "virsh dominfo " << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadCpuStat, this));
}

void VmStat::GetVcpuStat() {
    std::ostringstream cmd;
    cmd << "virsh vcpuinfo " << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadVcpuStat, this));
}

void VmStat::GetMemStat() {
    ReadMemStat();
}

void VmStat::ReadMemoryQuota() {
    std::string tmp;
    while (data_ >> tmp) {
        if (tmp == "actual") {
            data_ >> vm_memory_quota_;
        }
    }
    GetCpuStat();
}

void VmStat::GetMemoryQuota() {
    std::ostringstream cmd;
    cmd << "virsh dommemstat " << agent_->GetUuidStr(vm_uuid_);
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadMemoryQuota, this));
}

bool VmStat::TimerExpiry() {
    if (pid_ == 0) {
        GetPid();
    } else {
        //Get CPU and memory stats
        GetCpuStat();
    }
    return false;
}

void VmStat::StartTimer() {
    timer_->Cancel();
    timer_->Start(kTimeout, boost::bind(&VmStat::TimerExpiry, this));
}

void VmStat::ReadPid() {
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

void VmStat::GetPid() {
    std::ostringstream cmd;
    cmd << "ps -eo pid,cmd | grep " << agent_->GetUuidStr(vm_uuid_)
        << " | grep instance-";
    ExecCmd(cmd.str(), boost::bind(&VmStat::ReadPid, this));
}

void VmStat::Start() {
    GetPid();
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

