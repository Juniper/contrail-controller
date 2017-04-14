/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/task.h>
#endif

#include "sys/times.h"
#include <cstdlib>
#include <base/cpuinfo.h>

#include <fstream>
#include <iostream>

#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/find_iterator.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace boost;

static bool error_check(std::ifstream &file) {
    if (!file.is_open() || file.fail() || file.bad()) {
        return false;
    }
    return true;
}

static int NumCpus() {
    static int count = 0;

    if (count != 0) {
        return count;
    }

#ifdef __APPLE__
    size_t len = sizeof(count);
    sysctlbyname("hw.logicalcpu", &count, &len, NULL, 0);
    return count;
#else
    std::ifstream file("/proc/cpuinfo");
    if (!error_check(file)) {
        return -1;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
    // Create a find_iterator
    typedef find_iterator<std::string::iterator> string_find_iterator;

    for(string_find_iterator it = 
        make_find_iterator(content, first_finder("model name", is_iequal()));
        it!=string_find_iterator(); ++it, count++);
    return count;
#endif
}

static bool LoadAvg(CpuLoad &load) {
    double averages[3];
    int num_cpus = NumCpus();
    if (num_cpus < 0) {
        return false;
    }
    getloadavg(averages, 3);
    if (num_cpus > 0) {
        load.one_min_avg = averages[0]/num_cpus;
        load.five_min_avg = averages[1]/num_cpus;
        load.fifteen_min_avg = averages[2]/num_cpus;
    }
    return true;
}

static bool ProcessMemInfo(ProcessMemInfo &info) {
#ifdef __APPLE__
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    if (KERN_SUCCESS != task_info(mach_task_self(),
                                  TASK_BASIC_INFO,
                                  (task_info_t)&t_info,
                                  &t_info_count)) {
        return;
    }
    info.res = t_info.resident_size;
    info.virt = t_info.virtual_size;
    // XXX Peak virt not availabe, just fill in virt
    info.peakvirt = t_info.virtual_size;
    return true;
#else
    std::ifstream file("/proc/self/status");
    if (!error_check(file)) {
        return false;
    }
    bool vmsize = false;
    bool peak = false;
    bool rss = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("VmSize") != std::string::npos) {
            std::stringstream vm(line);
            std::string tmp; vm >> tmp; vm >> info.virt;
            vmsize = true;
        }   
        if (line.find("VmRSS") != std::string::npos) {
            std::stringstream vm(line);
            std::string tmp; vm >> tmp; vm >> info.res;
            rss = true;
        }   
        if (line.find("VmPeak") != std::string::npos) {
            std::stringstream vm(line);
            std::string tmp; vm >> tmp; vm >> info.peakvirt;
            peak = true;
        }   
        if (rss && vmsize && peak)
            break;
    }
    return true;
#endif
}

static bool SystemMemInfo(SystemMemInfo &info) {
    std::ifstream file("/proc/meminfo");
    if (!error_check(file)) {
        return false;
    }
    std::string tmp;
    // MemTotal:       132010576 kB
    file >> tmp; file >> info.total; file >> tmp; 
    // MemFree:        90333184 kB
    file >> tmp; file >> info.free; file >> tmp; 
    // Buffers:         1029924 kB
    file >> tmp; file >> info.buffers; file >> tmp;
    // Cached:         10290012 kB
    file >> tmp; file >> info.cached;
    // Used = Total - Free
    info.used = info.total - info.free;
    return true;
}

static clock_t snapshot, prev_sys_cpu, prev_user_cpu;

static bool ProcessCpuShare(double &percentage) {
    struct tms cpu_taken;
    clock_t now;

    now = times(&cpu_taken);
    if (now <= snapshot || cpu_taken.tms_stime < prev_sys_cpu ||
        cpu_taken.tms_utime < prev_user_cpu) {
        percentage = -1.0;
    } else {
        percentage = 
            (double)((cpu_taken.tms_stime - prev_sys_cpu) + 
                     (cpu_taken.tms_utime - prev_user_cpu)) / (now - snapshot);
        percentage *= 100;
        int num_cpus = NumCpus();
        if (num_cpus < 0) {
            return false;
        }
        percentage /= num_cpus;
    }
    snapshot = now;
    prev_sys_cpu = cpu_taken.tms_stime;
    prev_user_cpu = cpu_taken.tms_utime;
    return true;
}

bool CpuLoadData::GetCpuLoadInfo(CpuInfo &info, bool system) {
    if (system) {
        if (!LoadAvg(info.load_avg)) {
            return false;
        }
        if (!SystemMemInfo(info.sys_mem_info)) {
            return false;
        }
    }

    if (!ProcessMemInfo(info.mem_info)) {
        return false;
    }

    if (!ProcessCpuShare(info.process_cpu_share)) {
        return false;
    }
    info.num_cpu = NumCpus();
    return true;
}

void CpuLoadData::Init() {
    struct tms cpu_taken;
    snapshot = times(&cpu_taken);
    prev_sys_cpu = cpu_taken.tms_stime;
    prev_user_cpu = cpu_taken.tms_utime;
}

bool CpuLoadData::FillCpuInfo(CpuLoadInfo &cpu_load_info, bool system) {
    CpuInfo info;
    if (!CpuLoadData::GetCpuLoadInfo(info, system)) {
        // Error occured while getting the information
        return false;
    } else {
        MemInfo mem_info;
        mem_info.set_virt(info.mem_info.virt);
        mem_info.set_peakvirt(info.mem_info.peakvirt);
        mem_info.set_res(info.mem_info.res);
        cpu_load_info.set_meminfo(mem_info);

        cpu_load_info.set_cpu_share(info.process_cpu_share);

        if (system) {
            CpuLoadAvg load_avg;
            load_avg.set_one_min_avg(info.load_avg.one_min_avg);
            load_avg.set_five_min_avg(info.load_avg.five_min_avg);
            load_avg.set_fifteen_min_avg(info.load_avg.fifteen_min_avg);
            cpu_load_info.set_cpuload(load_avg);

            SysMemInfo sys_mem_info;
            sys_mem_info.set_total(info.sys_mem_info.total);
            sys_mem_info.set_used(info.sys_mem_info.used);
            sys_mem_info.set_free(info.sys_mem_info.free);
            sys_mem_info.set_buffers(info.sys_mem_info.buffers);
            sys_mem_info.set_cached(info.sys_mem_info.cached);
            cpu_load_info.set_sys_mem_info(sys_mem_info);
        }
    }
    return true;
}

void CpuLoadInfoReq::HandleRequest() const {
    CpuLoadInfo cpu_load_info;
    bool status = CpuLoadData::FillCpuInfo(cpu_load_info, true);
    CpuLoadInfoResp *resp = new CpuLoadInfoResp;
    if (status) {
        resp->set_cpu_info(cpu_load_info);
    }
    resp->set_context(context());
    resp->Response();
}

void PopulateProcessCpuInfo(const CpuLoadInfo &cpu_load_info,
    ProcessCpuInfo *pinfo) {
    pinfo->set_module_id(Sandesh::module());
    pinfo->set_inst_id(Sandesh::instance_id());
    pinfo->set_cpu_share(cpu_load_info.get_cpu_share());
    pinfo->set_mem_virt(cpu_load_info.get_meminfo().get_virt());
    pinfo->set_mem_res(cpu_load_info.get_meminfo().get_res());
}
