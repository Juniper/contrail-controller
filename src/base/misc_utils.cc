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
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace std;
namespace fs = boost::filesystem;
const std::string MiscUtils::ContrailVersionCmd = "/usr/bin/contrail-version";
const std::string MiscUtils::CoreFileDir = "/var/crashes/";
const int MiscUtils::MaxCoreFiles = 5;
const map<MiscUtils::BuildModule, string> MiscUtils::BuildModuleNames = 
    MiscUtils::MapInit();

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
            files_map.insert(FileMMap::value_type(fs::last_write_time
                                                    (itr->path()), file));
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

bool MiscUtils::GetContrailVersionInfo(BuildModule id, string &rpm_version, 
                                       string &build_num) {
    bool ret1, ret2;
    stringstream build_id_cmd;
    ifstream f(ContrailVersionCmd.c_str());
    if (!f.good()) {
        f.close();
        rpm_version.assign("unknown");
        build_num.assign("unknown");
        return false;
    }
    f.close();
    build_id_cmd << ContrailVersionCmd << " | grep '"
                 << BuildModuleNames.at(id) << "' | awk '{ print $2 }'";
    ret1 = GetVersionInfoInternal(build_id_cmd.str(), rpm_version);

    stringstream build_num_cmd;
    build_num_cmd << ContrailVersionCmd << " | grep '"
                  << BuildModuleNames.at(id) << "' | awk '{ print $3 }'";
    ret2 = GetVersionInfoInternal(build_num_cmd.str(), build_num);

    if (!ret1 || !ret2) {
        return false;
    }
    return true;
}

bool MiscUtils::GetBuildInfo(BuildModule id, const string &build_info, 
                             string &result) {
    string rpm_version;
    string build_num;

    bool ret = GetContrailVersionInfo(id, rpm_version, build_num);
    rapidjson::Document d;
    if (d.Parse<0>(const_cast<char *>(build_info.c_str())).HasParseError()) {
        result = build_info;
        return false;
    }
    rapidjson::Value& fields = d["build-info"];
    if (!fields.IsArray()) {
        result = build_info;
        return false;
    }
    fields[0u].AddMember("build-id", const_cast<char *>(rpm_version.c_str()), 
                         d.GetAllocator());
    fields[0u].AddMember("build-number", const_cast<char *>(build_num.c_str()), 
                         d.GetAllocator());

    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    d.Accept(writer);
    result = strbuf.GetString();
    return ret;
}

