/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef dns_named_config_h_
#define dns_named_config_h_

#include <iostream>
#include <fstream>
#include <set>
#include <string>
#include <base/timer.h>

class BindStatus {
public:
    static const uint32_t kBindStatusTimeout = 2 * 1000;
    static const uint32_t kInitTimeout = 200;
    enum Event {
        Up,
        Down
    };
    typedef boost::function<void(Event)> BindEventHandler;

    BindStatus(BindEventHandler handler);
    virtual ~BindStatus();
    bool SetTrigger();
    bool IsUp() { return (named_pid_ != (uint32_t) -1); }

private:
    friend class DnsBindTest;

    bool IsBindPid(uint32_t pid);
    bool CheckBindStatus();

    uint32_t named_pid_;
    TaskTrigger trigger_;
    BindEventHandler handler_;
    Timer *status_timer_;

    DISALLOW_COPY_AND_ASSIGN(BindStatus);
};

class NamedConfig {
public:
    // map of zone name to list of views to which they belong
    typedef std::map<std::string, std::string> ZoneViewMap;
    typedef std::pair<std::string, std::string> ZoneViewPair;

    static const std::string NamedZoneFileSuffix;
    static const std::string NamedZoneNSPrefix;
    static const std::string NamedZoneMXPrefix;
    static const char pid_file_name[];
    static const int NameWidth = 30;
    static const int NumberWidth = 10;
    static const int TypeWidth = 4;

    struct Defaults {
        static const int GlobalTTL = 86400;
        static const int Serial = 54;
        static const int Refresh = 10800;
        static const int Retry = 900;
        static const int Expire = 604800;
        static const int Minimum = 86400;
    };

    NamedConfig(const std::string& named_config_dir,
                const std::string& named_config_file,
                const std::string& named_log_file,
                const std::string& rndc_config_file,
                const std::string& rndc_secret,
                const std::string& named_max_cache_size) :
        file_(), named_log_file_(named_log_file), rndc_secret_(rndc_secret),
        named_max_cache_size_(named_max_cache_size),
        reset_flag_(false), all_zone_files_(false) {
            named_config_dir_ = named_config_dir + "/";
            named_config_file_ = named_config_dir_ + named_config_file;
            rndc_config_file_ = named_config_dir_ + rndc_config_file;
    }

    virtual ~NamedConfig() { singleton_ = NULL; }
    static NamedConfig *GetNamedConfigObject() { return singleton_; }
    static void Init(const std::string& named_config_dir,
                     const std::string& named_config_file,
                     const std::string& named_log_file,
                     const std::string& rndc_config_file,
                     const std::string& rndc_secret,
                     const std::string& named_max_cache_size);
    static void Shutdown();
    void Reset();
    virtual void AddView(const VirtualDnsConfig *vdns);
    virtual void ChangeView(const VirtualDnsConfig *vdns);
    virtual void DelView(const VirtualDnsConfig *vdns);
    virtual void AddAllViews();
    virtual void AddZone(const Subnet &subnet, const VirtualDnsConfig *vdns);
    virtual void DelZone(const Subnet &subnet, const VirtualDnsConfig *vdns);

    virtual void UpdateNamedConf(const VirtualDnsConfig *updated_vdns = NULL);
    void RemoveZoneFiles(const VirtualDnsConfig *vdns, ZoneList &zones);
    virtual std::string GetZoneFileName(const std::string &vdns, 
                                        const std::string &name);
    virtual std::string GetZoneFilePath(const std::string &vdns, 
                                        const std::string &name);
    virtual std::string GetResolveFile() { return "/etc/resolv.conf"; }
    std::string GetPidFilePath();
    const std::string &named_config_dir() const { return named_config_dir_; }
    const std::string &named_config_file() const { return named_config_file_; }

protected:
    void CreateRndcConf();
    void CreateNamedConf(const VirtualDnsConfig *updated_vdns);
    void WriteOptionsConfig();
    void WriteRndcConfig();
    void WriteLoggingConfig();
    void WriteViewConfig(const VirtualDnsConfig *updated_vdns);
    void WriteDefaultView(ZoneViewMap &zone_view_map);
    void WriteZone(const std::string &vdns, const std::string &name,
                   bool is_master, bool is_rr, const std::string &next_dns);
    void AddZoneFiles(ZoneList &zones, const VirtualDnsConfig *vdns);
    void RemoveZoneFile(const VirtualDnsConfig *vdns, std::string &zone);
    std::string GetZoneNSName(const std::string domain_name);
    std::string GetZoneMXName(const std::string domain_name);
    void CreateZoneFile(std::string &zone_name, 
                        const VirtualDnsConfig *vdns, bool ns);
    void MakeZoneList(const VirtualDnsConfig *vdns_config, ZoneList &zones);
    void MakeReverseZoneList(const VirtualDnsConfig *vdns_config,
                             ZoneList &zones);
    void GetDefaultForwarders();

    std::ofstream file_;
    std::string named_config_file_;
    std::string named_config_dir_;
    std::string named_log_file_;
    std::string rndc_config_file_;
    std::string rndc_secret_;
    std::string named_max_cache_size_;
    std::string default_forwarders_;
    bool reset_flag_;
    bool all_zone_files_;
    static NamedConfig *singleton_;
};

#endif // dns_named_config_h_
