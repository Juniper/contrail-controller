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

#endif // ctrlplane_cpuinfo_h 
