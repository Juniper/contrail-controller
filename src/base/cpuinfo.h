/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_cpuinfo_h
#define ctrlplane_cpuinfo_h

#include <inttypes.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include <base/sandesh/cpuinfo_types.h>

struct CpuLoad {
    double one_min_avg;
    double five_min_avg;
    double fifteen_min_avg;
};

struct ProcessMemInfo {
    uint32_t virt;
    uint32_t peakvirt;
    uint32_t res;
};

struct SystemMemInfo {
    uint32_t total;
    uint32_t used;
    uint32_t free;
    uint32_t buffers;
    uint32_t cached;
};

struct CpuInfo {
    uint32_t num_cpu;
    ProcessMemInfo mem_info;
    SystemMemInfo sys_mem_info;
    CpuLoad load_avg;
    double process_cpu_share;
};


class CpuLoadData {
public:
    static void GetCpuLoadInfo(CpuInfo &info, bool system);
    static void FillCpuInfo(CpuLoadInfo &info, bool system);
    static void Init();
};

void PopulateProcessCpuInfo(const CpuLoadInfo &cpu_load_info,
    ProcessCpuInfo *pinfo);

template <typename CpuInfoStatUveType, typename CpuInfoStatUveDataType>
void SendCpuInfoStat(const std::string &name,
    const CpuLoadInfo &cpu_load_info) {
    CpuInfoStatUveDataType data;
    data.set_name(name);
    ProcessCpuInfo pinfo;
    PopulateProcessCpuInfo(cpu_load_info, &pinfo);
    std::vector<ProcessCpuInfo> v_pinfo;
    v_pinfo.push_back(pinfo);
    data.set_cpu_info(v_pinfo);
    CpuInfoStatUveType::Send(data);
}

#endif // ctrlplane_cpuinfo_h 
