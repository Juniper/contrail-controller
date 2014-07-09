/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cstdio>
#include <iomanip>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include "base/logging.h"
#include <base/contrail_ports.h>
#include <bind/bind_util.h>
#include <cfg/dns_config.h>
#include <mgr/dns_oper.h>
#include "named_config.h"
#include <cmn/dns.h>

using namespace std;

const char NamedConfig::NamedConfigFile[] = "/etc/contrail/dns/named.conf";
const char NamedConfig::NamedLogFile[] = "/var/log/named/bind.log";
const char NamedConfig::RndcSecret[] = "xvysmOR8lnUQRBcunkC6vg==";
NamedConfig *NamedConfig::singleton_;
const string NamedConfig::NamedZoneFileSuffix = "zone";
const string NamedConfig::NamedZoneNSPrefix = "contrail-ns";
const string NamedConfig::NamedZoneMXPrefix = "contrail-mx";
const char NamedConfig::ZoneFileDirectory[] = "/etc/contrail/dns/";
const char NamedConfig::pid_file_name[] = "named.pid";

void NamedConfig::Init() {
    assert(singleton_ == NULL);
    singleton_ = new NamedConfig();
    singleton_->Reset();
}

void NamedConfig::Shutdown() {
    assert(singleton_);
    delete singleton_;
}

// Reset bind config 
void NamedConfig::Reset() {
    reset_flag_ = true;
    UpdateNamedConf();
    DIR *dir = opendir(ZoneFileDirectory);
    if (dir) {
        struct dirent *file;
        while ((file = readdir(dir)) != NULL) {
            std::string str(ZoneFileDirectory);
            str.append(file->d_name);
            if (str.find(".zone") != std::string::npos) {
                remove(str.c_str());
            }
        }
        closedir(dir);
    }
    reset_flag_ = false;
}

void NamedConfig::AddView(const VirtualDnsConfig *vdns) {
    UpdateNamedConf(vdns);
}

void NamedConfig::ChangeView(const VirtualDnsConfig *vdns) {
    UpdateNamedConf(vdns);
    std::string old_domain = vdns->GetOldDomainName();
    if (vdns->GetDomainName() != old_domain) {
        ZoneList zones;
        zones.push_back(old_domain);
        RemoveZoneFiles(vdns, zones);
    }
}

void NamedConfig::DelView(const VirtualDnsConfig *vdns) {
    UpdateNamedConf(vdns);
}

void NamedConfig::AddAllViews() {
    all_zone_files_ = true;
    UpdateNamedConf();
    all_zone_files_ = false;
}

void NamedConfig::AddZone(const Subnet &subnet, const VirtualDnsConfig *vdns) {
    ZoneList zones;
    BindUtil::GetReverseZones(subnet, zones);
    // Ignore zone files which already exist
    for (unsigned int i = 0; i < zones.size();) {
        std::ifstream file(zones[i].c_str());
        if (file.good()) {
            zones.erase(zones.begin() + i);
        } else
            i++;
    }
    AddZoneFiles(zones, vdns);
    UpdateNamedConf();
}

void NamedConfig::DelZone(const Subnet &subnet, const VirtualDnsConfig *vdns) {
    UpdateNamedConf();
    ZoneList vdns_zones, snet_zones;
    MakeZoneList(vdns, vdns_zones);
    BindUtil::GetReverseZones(subnet, snet_zones);
    // Ignore zones which are still in use
    for (unsigned int i = 0; i < snet_zones.size();) {
        unsigned int j;
        for (j = 0; j < vdns_zones.size(); j++) {
            if (snet_zones[i] == vdns_zones[j]) {
                snet_zones.erase(snet_zones.begin() + i);
                break;
            }
        }
        if (j == vdns_zones.size())
            i++;
    }
    RemoveZoneFiles(vdns, snet_zones);
}

void NamedConfig::UpdateNamedConf(const VirtualDnsConfig *updated_vdns) {
    CreateNamedConf(updated_vdns);
    sync();
    // rndc_reconfig();
    // TODO: convert this to a call to rndc library
    std::stringstream str;
    str << "/usr/bin/rndc -c /etc/contrail/dns/rndc.conf -p ";
    str << ContrailPorts::DnsRndc;
    str << " reconfig";
    int res = system(str.str().c_str());
    if (res) {
        LOG(WARN, "/usr/bin/rndc command failed");
    }
}

void NamedConfig::CreateNamedConf(const VirtualDnsConfig *updated_vdns) {
     GetDefaultForwarders();
     file_.open(named_conf_file_.c_str());
    
     WriteOptionsConfig();
     WriteRndcConfig();
     WriteLoggingConfig();
     WriteViewConfig(updated_vdns);

     file_.flush();
     file_.close();
}

void NamedConfig::WriteOptionsConfig() {
    file_ << "options {" << endl;
    file_ << "    directory \"" << zone_file_dir_ << "\";" << endl;
    file_ << "    managed-keys-directory \"" << zone_file_dir_ << "\";" << endl;
    file_ << "    empty-zones-enable no;" << endl;
    file_ << "    pid-file \"" << GetPidFilePath() << "\";" << endl;
    file_ << "    listen-on port " << Dns::GetDnsPort() << " { any; };" << endl;
    file_ << "    allow-query { any; };" << endl;
    file_ << "    allow-recursion { any; };" << endl;
    file_ << "    allow-query-cache { any; };" << endl;
    file_ << "};" << endl << endl;
}

void NamedConfig::WriteRndcConfig() {
    file_ << "key \"rndc-key\" {" << endl;
    file_ << "    algorithm hmac-md5;" << endl;
    file_ << "    secret \"" << RndcSecret << "\";" << endl;
    file_ << "};" << endl << endl;


    file_ << "controls {" << endl;
    file_ << "    inet 127.0.0.1 port "<< ContrailPorts::DnsRndc << endl;
    file_ << "    allow { 127.0.0.1; }  keys { \"rndc-key\"; };" << endl;
    file_ << "};" << endl << endl;
}

void NamedConfig::WriteLoggingConfig() {
    file_ << "logging {" << endl;
    file_ << "    channel debug_log {" << endl;
    file_ << "        file \"" << NamedLogFile << "\" versions 3 size 5m;" << endl;
    file_ << "        severity debug;" << endl;
    file_ << "        print-time yes;" << endl;
    file_ << "        print-severity yes;" << endl;
    file_ << "        print-category yes;" << endl;
    file_ << "    };" << endl;
    file_ << "    category default {" << endl;
    file_ << "        debug_log;" << endl;
    file_ << "    };" << endl;
    file_ << "    category queries {" << endl;
    file_ << "        debug_log;" << endl;
    file_ << "    };" << endl;
    file_ << "};" << endl << endl;
}

void NamedConfig::WriteViewConfig(const VirtualDnsConfig *updated_vdns) {
    ZoneViewMap zone_view_map;
    if (reset_flag_) {
        WriteDefaultView(zone_view_map);
        return;
    }

    VirtualDnsConfig::DataMap vdns = VirtualDnsConfig::GetVirtualDnsMap();
    for (VirtualDnsConfig::DataMap::iterator it = vdns.begin(); it != vdns.end(); ++it) {
        VirtualDnsConfig *curr_vdns = it->second;
        ZoneList zones;
        MakeZoneList(curr_vdns, zones);

        if (curr_vdns->IsDeleted() || !curr_vdns->IsNotified()) {
            RemoveZoneFiles(curr_vdns, zones);
            continue;
        }

        std::string view_name = curr_vdns->GetViewName();
        file_ << "view \"" << view_name << "\" {" << endl;

        std::string order = curr_vdns->GetRecordOrder();
        if (!order.empty()) {
            if (order == "round-robin")
                order = "cyclic";
            file_ << "    rrset-order {order " << order << ";};" << endl;
        }

        std::string next_dns = curr_vdns->GetNextDns();
        if (!next_dns.empty()) {
            boost::system::error_code ec;
            boost::asio::ip::address_v4 
                next_addr(boost::asio::ip::address_v4::from_string(next_dns, ec));
            if (!ec.value()) {
                file_ << "    forwarders {" << next_addr.to_string() << ";};" << endl;
            } else {
                file_ << "    virtual-forwarder \"" << next_dns << "\";" << endl;
            }
        } else if (!default_forwarders_.empty()) {
            file_ << "    forwarders {" << default_forwarders_ << "};" << endl;
        }

        for (unsigned int i = 0; i < zones.size(); i++) {
            WriteZone(view_name, zones[i], true);
            // update the zone view map, to be used to generate default view
            // TODO : if there are multiple views having the same zone,
            // we consider only the first one for now
            zone_view_map.insert(ZoneViewPair(zones[i], view_name));
        }

        file_ << "};" << endl << endl;

        if (curr_vdns == updated_vdns || all_zone_files_)
            AddZoneFiles(zones, curr_vdns);
    }

    WriteDefaultView(zone_view_map);
}

void NamedConfig::WriteDefaultView(ZoneViewMap &zone_view_map) {
    // Create a default view first for any requests which do not have 
    // view name TXT record
    file_ << "view \"_default_view_\" {" << endl;
    file_ << "    match-clients {any;};" << endl;
    file_ << "    match-destinations {any;};" << endl;
    file_ << "    match-recursive-only no;" << endl;
    if (!default_forwarders_.empty()) {
        file_ << "    forwarders {" << default_forwarders_ << "};" << endl;
    }
    for (ZoneViewMap::iterator it = zone_view_map.begin(); 
         it != zone_view_map.end(); ++it) {
        WriteZone(it->second, it->first, false);
    }
    file_ << "};" << endl << endl;
}

void NamedConfig::WriteZone(const string &vdns, const string &name,
                            bool is_master) {
    file_ << "    zone \"" << name << "\" IN \{" << endl;
    if (is_master) {
        file_ << "        type master;" << endl;
        file_ << "        file \"" << GetZoneFilePath(vdns, name) << "\";" << endl;
        file_ << "        allow-update {127.0.0.1;};" << endl;
    } else {
        file_ << "        type static-stub;" << endl;
        file_ << "        virtual-server-name \"" << vdns << "\";" << endl;
        file_ << "        server-addresses {127.0.0.1;};" << endl;
    }
    file_ << "    };" << endl;
}

void NamedConfig::AddZoneFiles(ZoneList &zones, const VirtualDnsConfig *vdns) {
    for (unsigned int i = 0; i < zones.size(); i++) {
        bool ns = (zones[i].find("in-addr.arpa") == std::string::npos);
        CreateZoneFile(zones[i], vdns, ns);
    }
}

void NamedConfig::RemoveZoneFiles(const VirtualDnsConfig *vdns, 
                                  ZoneList &zones) {
    for (unsigned int i = 0; i < zones.size(); i++) {
        RemoveZoneFile(vdns, zones[i]);
    }
}

void NamedConfig::RemoveZoneFile(const VirtualDnsConfig *vdns, string &zone) {
    string zfile_name = GetZoneFilePath(vdns->GetViewName(), zone);
    remove(zfile_name.c_str());
    zfile_name.append(".jnl");
    remove(zfile_name.c_str());
}

string NamedConfig::GetZoneFileName(const string &vdns, const string &name) {
    if (name.size() && name.at(name.size() - 1) == '.')
        return (vdns + "." + name + NamedZoneFileSuffix);
    else
        return (vdns + "." + name + "." + NamedZoneFileSuffix);
}

string NamedConfig::GetZoneFilePath(const string &vdns, const string &name) {
    return (zone_file_dir_ + GetZoneFileName(vdns, name));
}

string NamedConfig::GetPidFilePath() {
    return (zone_file_dir_ + pid_file_name);
}

string NamedConfig::GetZoneNSName(const string domain_name) {
    return (NamedZoneNSPrefix + "." + domain_name);
}

string NamedConfig::GetZoneMXName(const string domain_name) {
    return (NamedZoneMXPrefix + "." + domain_name);
}

void NamedConfig::CreateZoneFile(std::string &zone_name, 
                                 const VirtualDnsConfig *vdns, bool ns) {
    ofstream zfile;
    string ns_name;
    string zone_filename = GetZoneFilePath(vdns->GetViewName(), zone_name);

    zfile.open(zone_filename.c_str());
    zfile << "$ORIGIN ." << endl;
    if (vdns->GetTtl() > 0) {
        zfile << "$TTL " << vdns->GetTtl() << endl;
    } else {
        zfile << "$TTL " << Defaults::GlobalTTL << endl;
    }
    zfile << left << setw(NameWidth) << zone_name << " IN  SOA " <<
                GetZoneNSName(vdns->GetDomainName()) << "  " << 
                GetZoneMXName(vdns->GetDomainName()) << " (" << endl;
    zfile << setw(NameWidth + 8) << "" << setw(NumberWidth) << Defaults::Serial << endl;
    zfile << setw(NameWidth + 8) << "" << setw(NumberWidth) << Defaults::Refresh << endl;
    zfile << setw(NameWidth + 8) << "" << setw(NumberWidth) << Defaults::Retry << endl;
    zfile << setw(NameWidth + 8) << "" << setw(NumberWidth) << Defaults::Expire << endl;
    zfile << setw(NameWidth + 8) << "" << setw(NumberWidth) << Defaults::Minimum << endl;
    zfile << setw(NameWidth + 8) << "" << ")" << endl;
    /* NS records are mandatory in zone file. They are required for the following reasons
       1. Name servers returns NS RR in responses to queries, in the authority section 
          of the DNS message.
       2. Name servers use the NS records to determine where to send NOTIFY messages.
     */
    zfile << setw(NameWidth + 4) << "" << setw(TypeWidth) << " NS " << 
                setw(NameWidth) << GetZoneNSName(vdns->GetDomainName()) << endl;
    zfile << "$ORIGIN " << zone_name << endl;
    //Write the NS record
    if (ns)
        zfile << setw(NameWidth) << NamedZoneNSPrefix << " IN  A   " << Dns::GetSelfIp() << endl;
    zfile.flush();
    zfile.close();
}

// Create a list of zones for the virtual DNS
void NamedConfig::MakeZoneList(const VirtualDnsConfig *vdns_config, ZoneList &zones) {
    std::string dns_domain = vdns_config->GetDomainName();
    if (dns_domain.empty()) {
        return;
    }

    // Forward Zone
    zones.push_back(dns_domain);

    // Reverse zones
    const VirtualDnsConfig::IpamList &ipams = vdns_config->GetIpamList();
    for (VirtualDnsConfig::IpamList::const_iterator ipam_it = ipams.begin();
         ipam_it != ipams.end(); ++ipam_it) {
        if ((*ipam_it)->IsDeleted() || !(*ipam_it)->IsValid()) {
            continue;
        }
        const IpamConfig::VnniList &vnni_list = (*ipam_it)->GetVnniList();
        for (IpamConfig::VnniList::iterator vnni_it = vnni_list.begin();
             vnni_it != vnni_list.end(); ++vnni_it) {
            if ((*vnni_it)->IsDeleted() || !(*vnni_it)->IsValid()) {
                continue;
            }
            const Subnets &subnets = (*vnni_it)->GetSubnets();
            for (unsigned int i = 0; i < subnets.size(); ++i) {
                const Subnet &subnet = subnets[i];
                if (subnet.IsDeleted())
                    continue;
                BindUtil::GetReverseZones(subnet, zones);
            }
        }
    }

    // If same subnet is used in different VNs, remove duplicates
    std::sort(zones.begin(), zones.end());
    ZoneList::iterator it = std::unique(zones.begin(), zones.end());
    zones.resize(std::distance(zones.begin(), it));
}

void NamedConfig::GetDefaultForwarders() {
    default_forwarders_.clear();
    std::ifstream fd;
    fd.open(GetResolveFile().c_str());
    if (!fd.is_open()) {
        return;
    }

    std::string line;
    while (getline(fd, line)) {
        std::size_t pos = line.find_first_of("#");
        std::stringstream ss(line.substr(0, pos));
        std::string key;
        ss >> key;
        if (key == "nameserver") {
            std::string ip;
            ss >> ip;
            boost::system::error_code ec;
            boost::asio::ip::address_v4::from_string(ip, ec);
            if (!ec.value()) {
                default_forwarders_ += ip + "; ";
            }
        }
    }

    fd.close();
}

///////////////////////////////////////////////////////////////////////////////

BindStatus::BindStatus(BindEventHandler handler) 
    : named_pid_(-1),
      trigger_(boost::bind(&BindStatus::CheckBindStatus, this),
               TaskScheduler::GetInstance()->GetTaskId("dns::BindStatus"), 0),
      handler_(handler) {
    status_timer_ = TimerManager::CreateTimer(
                    *Dns::GetEventManager()->io_service(), "BindStatusTimer");
    status_timer_->Start(kInitTimeout, 
                         boost::bind(&BindStatus::SetTrigger, this));
}

BindStatus::~BindStatus() {
    status_timer_->Cancel();
    TimerManager::DeleteTimer(status_timer_);
}

bool BindStatus::SetTrigger() {
    trigger_.Set();
    return false;
}

bool BindStatus::CheckBindStatus() {
    uint32_t new_pid = -1;
    NamedConfig *ncfg = NamedConfig::GetNamedConfigObject();
    if (ncfg) {
        std::ifstream pid_file(ncfg->GetPidFilePath().c_str());
        if (pid_file.good()) {
            pid_file >> new_pid;
        }
        pid_file.close();
    }

    if (new_pid != named_pid_) {
        named_pid_ = new_pid;
        if (new_pid == (uint32_t) -1) {
            handler_(Down);
        } else {
            handler_(Up);
        }
    }

    status_timer_->Start(kBindStatusTimeout, 
                         boost::bind(&BindStatus::SetTrigger, this));
    return true;
}
