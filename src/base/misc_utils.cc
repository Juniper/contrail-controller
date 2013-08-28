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
            inet_ntop(AF_INET,&(addr.sin_addr.s_addr),buff,sizeof(buff));
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

void MiscUtils::GetContrailVersionInfo(BuildModule id, string &rpm_version, string &build_num) {
    //Store rpm version in a tmp file
    string cmd = "contrail-version | grep " + BuildModuleNames.at(id);
    system((cmd + "  | awk '{ print $2 }' > .tmp.build.info").c_str());
    system((cmd + "  | awk '{ print $3 }' >> .tmp.build.info").c_str());

    //Read rpm Version from the tmp File
    ifstream file;
    file.open(".tmp.build.info");

    if (file.good()) {
        getline(file, rpm_version);
        if (file.good()) { 
            getline(file, build_num);
        } else {
            build_num = "unknown";
        }
    } else {
        rpm_version = "unknown";
        build_num = "unknown";
    }
    file.close();
    remove(".tmp.build.info");
}

string MiscUtils::GetBuildInfo(BuildModule id, string build_info) {
    string rpm_version;
    string build_num;

    GetContrailVersionInfo(id, rpm_version, build_num);
    return (build_info + "\"build-id\" : \"" + rpm_version + "\", \"build-number\" : \"" + build_num  + "\"}]}");
}

