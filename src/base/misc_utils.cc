/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <stdlib.h> 
#include <base/misc_utils.h>
#include <base/logging.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include "base/sandesh/version_types.h"
#include "base/logging.h"

using namespace std;
namespace fs = boost::filesystem;
const std::string MiscUtils::CoreFileDir = "/var/crashes/";
const int MiscUtils::MaxCoreFiles = 5;
const map<MiscUtils::BuildModule, string> MiscUtils::BuildModuleNames = MiscUtils::MapInit();

SandeshTraceBufferPtr VersionTraceBuf(SandeshTraceBufferCreate(
                                       VERSION_TRACE_BUF, 500));

string MiscUtils::BaseName(string filename) {
    size_t pos = filename.find_last_of('/');
    if (pos != string::npos) {
        return filename.substr((pos+1));
    }
    return filename;
}

void MiscUtils::LogVersionInfo(const string build_info, Category::type categ) {
    VERSION_TRACE(VersionInfoTrace, build_info);
    if (!LoggingDisabled()) {
        VERSION_LOG(VersionInfoLog, categ, build_info);
    }
}

void MiscUtils::GetCoreFileList(string prog, vector<string> &list) {
    if (!fs::exists(CoreFileDir) || !fs::is_directory(CoreFileDir)) {
        return;
    }
    FileMMap files_map;
    
    string filename = "core." + BaseName(prog) + ".";

    fs::path dir_path(CoreFileDir.c_str());
    fs::directory_iterator end_itr;
    for (fs::directory_iterator itr(dir_path); itr != end_itr; itr++) {
        if (fs::is_regular_file(itr->status())) {
            const string file = itr->path().filename().generic_string();
            size_t pos = file.find(filename);
            if (pos != 0) {
                continue;
            }
            files_map.insert(FileMMap::value_type(fs::last_write_time(itr->path()), file));
        }
    }
    FileMMap::reverse_iterator rit;
    int count = 0;
    for (rit = files_map.rbegin(); rit != files_map.rend() && 
        count < MaxCoreFiles; ++rit) {
        count++;
        list.push_back(rit->second);
    }
}

void MiscUtils::GetHostIp(const string hostname, vector<string> &ip_list) {
    struct hostent *host;
    host = gethostbyname(hostname.c_str());
    if (host == NULL) {
        return;
    }
    if (host->h_addrtype == AF_INET) {
        char buff[INET_ADDRSTRLEN];
        struct sockaddr_in addr;
        int len = 0, i = 0;
        string ip_addr;
        while (len < host->h_length) {
            addr.sin_addr = *(struct in_addr *)host->h_addr_list[i++];
            addr.sin_family = host->h_addrtype;
            inet_ntop(AF_INET, &(addr.sin_addr.s_addr), buff, sizeof(buff));
            len += 4;
            ip_addr = string(buff);
            //ignore loop back addresses
            int pos = ip_addr.find("127");
            if (pos == 0) {
                continue;
            }
            ip_list.push_back(ip_addr);
        }
    }
}

bool MiscUtils::GetVersionInfoInternal(const string &cmd, string &result) {
    FILE *fp;
    char line[512];
    fp = popen(cmd.c_str(), "r");
    if (fp == NULL) {
        result.assign("unknown");
        return false;
    }
    char *ptr = fgets(line, sizeof(line), fp);
    if (ptr == NULL) {
        result.assign("unknown");
        pclose(fp);
        return false;
    }
    ptr = strchr(line, '\n');
    if (ptr != NULL) {
        *ptr = '\0';
    }
    result.assign(line);
    pclose(fp);
    return true;
}

bool MiscUtils::GetContrailVersionInfo(BuildModule id, string &rpm_version, string &build_num) {
    bool ret1, ret2;
    stringstream build_id_cmd;
    build_id_cmd << "contrail-version | grep '" << BuildModuleNames.at(id);
    build_id_cmd << "' | awk '{ print $2 }'";
    ret1 = GetVersionInfoInternal(build_id_cmd.str(), rpm_version);

    stringstream build_num_cmd;
    build_num_cmd << "contrail-version | grep '" << BuildModuleNames.at(id);
    build_num_cmd << "' | awk '{ print $3 }'";
    ret2 = GetVersionInfoInternal(build_num_cmd.str(), build_num);

    if (!ret1 || !ret2) {
        return false;
    }
    return true;
}

bool MiscUtils::GetBuildInfo(BuildModule id, const string &build_info, string &result) {
    string rpm_version;
    string build_num;

    bool ret = GetContrailVersionInfo(id, rpm_version, build_num);
    result = (build_info + "\"build-id\" : \"" + rpm_version + "\", \"build-number\" : \"" + build_num  + "\"}]}");
    return ret;
}

