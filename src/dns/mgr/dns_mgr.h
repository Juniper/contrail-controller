/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __dns_manager_h__
#define __dns_manager_h__

#include <tbb/mutex.h>
#include <cfg/dns_config.h>
#include <bind/bind_util.h>
#include <bind/named_config.h>

class DnsManager {
public:
    DnsManager();
    virtual ~DnsManager();
    void Initialize(DB *config_db, DBGraph *config_graph);
    void Shutdown();
    void DnsView(const VirtualDnsConfig *config, 
                 DnsConfigManager::EventType ev);
    void DnsPtrZone(const Subnet &subnet, const VirtualDnsConfig *vdns,
                    DnsConfigManager::EventType ev);
    void DnsRecord(const VirtualDnsRecordConfig *config,
                   DnsConfigManager::EventType ev);
    void HandleUpdateResponse(uint8_t *pkt);
    DnsConfigManager &GetConfigManager() { return config_mgr_; }
    void SendUpdate(BindUtil::Operation op, const std::string &view,
                    const std::string &zone, DnsItems &items);
    void UpdateAll();
    void BindEventHandler(BindStatus::Event ev);

private:
    friend class DnsBindTest;

    void SendRecordUpdate(BindUtil::Operation op, 
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
