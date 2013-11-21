/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __dns_manager_h__
#define __dns_manager_h__

#include <tbb/mutex.h>
#include <mgr/dns_oper.h>
#include <bind/named_config.h>
#include <cfg/dns_config.h>

class DB;
class DBGraph;
struct VirtualDnsConfig;
struct VirtualDnsRecordConfig;

class DnsManager {
public:
    static const int max_records_per_sandesh = 200;

    DnsManager();
    virtual ~DnsManager();
    void Initialize(DB *config_db, DBGraph *config_graph);
    void Shutdown();
    void DnsView(const DnsConfig *config, DnsConfig::DnsConfigEvent ev);
    void DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                    DnsConfig::DnsConfigEvent ev);
    void DnsRecord(const DnsConfig *config, DnsConfig::DnsConfigEvent ev);
    void HandleUpdateResponse(uint8_t *pkt);
    DnsConfigManager &GetConfigManager() { return config_mgr_; }
    void SendUpdate(BindUtil::Operation op, const std::string &view,
                    const std::string &zone, DnsItems &items);
    void UpdateAll();
    void BindEventHandler(BindStatus::Event ev);

    template <typename ConfigType>
    void ProcessConfig(IFMapNodeProxy *proxy, const std::string &name,
                       DnsConfigManager::EventType event);
    void ProcessAgentUpdate(BindUtil::Operation event, const std::string &name,
                            const std::string &vdns_name, const DnsItem &item);

private:
    friend class DnsBindTest;

    bool SendRecordUpdate(BindUtil::Operation op, 
                          const VirtualDnsRecordConfig *config);
    inline uint16_t GetTransId();
    inline bool CheckName(std::string rec_name, std::string name);

    tbb::mutex mutex_;
    BindStatus bind_status_;
    DnsConfigManager config_mgr_;    
    static uint16_t g_trans_id_;

    DISALLOW_COPY_AND_ASSIGN(DnsManager);
};

#endif // __dns_manager_h__
