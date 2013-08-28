/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/misc_utils.h>
#include <cmn/dns.h>
#include <uve/uve.h>
#include <string>
#include <vector>

using namespace std;
DnsState DnsUveClient::prev_state_;

void DnsUveClient::SendDnsUve(uint64_t start_time) {
    DnsState state;
    boost::system::error_code ec;
    static bool first = true;
    bool changed = false;

    state.set_name(Dns::GetHostName());
    if (first) {
        state.set_start_time(start_time);
        state.set_collector(Dns::GetCollector());
        state.set_build_info(Dns::GetVersion());

        vector<string> list;
        MiscUtils::GetCoreFileList(Dns::GetProgramName(), list);
        state.set_core_files_list(list);
        vector<string> ip_list;
        ip_list.push_back(Dns::GetSelfIp());
        state.set_self_ip_list(ip_list);
        first = false;
        changed = true;
    }

    if (changed) {
        UveDnsInfo::Send(state);
    }
}

