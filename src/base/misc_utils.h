/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_misc_utils_h
#define ctrlplane_misc_utils_h

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <string>
#include <vector>
#include <map>
#include <ctime>

#define VERSION_TRACE_BUF "VersionTrace"

extern SandeshTraceBufferPtr VersionTraceBuf;

#define VERSION_TRACE(obj, ...) do {                                  \
    obj::TraceMsg(VersionTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0);

#define VERSION_LOG(obj, categ, ...)\
    do {\
            obj::Send(g_vns_constants.CategoryNames.find(categ)->second,\
                                  SandeshLevel::SYS_INFO, __FILE__, __LINE__, ##__VA_ARGS__);\
    } while (false);



class MiscUtils {
public:
    enum BuildModule {
        Agent,
        Analytics,
        ControlNode,
        Dns,
        MaxModules
    };
    static std::map<BuildModule, std::string> MapInit() {
        std::map<BuildModule, std::string> m;
         m[Agent] = "contrail-vrouter-agent ";
         m[Analytics] = "contrail-analytics ";
         m[ControlNode] = "contrail-control ";
         m[Dns] = "contrail-dns ";
         return m;
    }
    static const std::map<BuildModule, std::string> BuildModuleNames;
    typedef std::multimap<std::time_t, std::string> FileMMap;
    static const std::string CoreFileDir;
    static const int MaxCoreFiles;

    static void GetCoreFileList(std::string prog, std::vector<std::string> &list);
    static bool GetBuildInfo(BuildModule id, const std::string &build_info, std::string &result);
    static void GetHostIp(const std::string name, std::vector<std::string> &ip_list);
    static void LogVersionInfo(const std::string str, Category::type categ);
private:
    static bool GetContrailVersionInfo(BuildModule id, std::string &rpm_version, std::string &build_num);
    static std::string BaseName(std::string filename);
    static bool GetVersionInfoInternal(const std::string &cmd, std::string &result);
};

#endif // ctrlplane_misc_utils_h 
