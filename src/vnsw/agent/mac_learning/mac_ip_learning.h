/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_IP_LEARNING_MAC_LEARNING_H_
#define SRC_VNSW_AGENT_MAC_IP_LEARNING_MAC_LEARNING_H_

#include "cmn/agent.h"
#include "mac_ip_learning_key.h"
#include "mac_learning_base.h"
#include "mac_learning_event.h"
#include "pkt/flow_token.h"
#include "health_check.h"

class MacIpLearningTable;
class MacIpLearningRequestQueue;
class MacIpLearningEntry : public MacLearningEntry {
public:
    MacIpLearningEntry(MacIpLearningTable *table, uint32_t vrf_id_,
            const IpAddress &ip, const MacAddress &mac,
            InterfaceConstRef intf);
    virtual ~MacIpLearningEntry() {}
    bool Add();
    void Delete();
    void Resync();
    uint32_t vrf_id()  {
        return key_.vrf_id_;
    }
    const Interface* intf() {
        return intf_.get();
    }
    const VnEntry* Vn() {
        return vn_.get();
    }
    const HealthCheckService* HcService() {
        return hc_service_;
    }
    const HealthCheckInstanceBase* HcInstance() {
        return hc_instance_;
    }
    MacIpLearningTable *mac_ip_learning_table() const {
        return mac_ip_learning_table_;
    }
    const MacIpLearningKey& key() const {
        return key_;
    }
    IpAddress& IpAddr() {
        return ip_;
    }
    MacAddress& Mac() {
        return mac_;
    }
    void EnqueueToTable(MacLearningEntryRequestPtr req);
    void AddHealthCheckService(HealthCheckService *service);
    void UpdateHealthCheckService();
    HealthCheckService* GetHealthCheckService(const VnEntry *vn);
    
private:
    MacIpLearningTable *mac_ip_learning_table_;
    MacIpLearningKey key_;
    IpAddress ip_;
    MacAddress mac_;
    InterfaceConstRef intf_;
    VnEntryConstRef vn_;
    HealthCheckService *hc_service_;
    HealthCheckInstanceBase  *hc_instance_;
    DISALLOW_COPY_AND_ASSIGN(MacIpLearningEntry);
};

class MacIpLearningTable {
public:
    typedef std::pair<MacIpLearningKey,
                      MacLearningEntryPtr> MacIpLearningEntryPair;
    typedef std::map<MacIpLearningKey,
                     MacLearningEntryPtr,
                     MacIpLearningKeyCmp> MacIpLearningEntryMap;

    MacIpLearningTable(Agent *agent, MacLearningProto *proto);
    virtual ~MacIpLearningTable() {}
    void Add(MacLearningEntryPtr ptr);
    void Resync(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);
    void DetectIpMove(MacLearningEntryRequestPtr ptr);
    void MacIpEntryUnreachable(MacLearningEntryRequestPtr ptr);
    void MacIpEntryUnreachable(uint32_t vrf_id, IpAddress &ip, MacAddress &mac);
    MacIpLearningEntry* Find(const MacIpLearningKey &key);
    MacAddress GetPairedMacAddress(uint32_t vrf_id, const IpAddress &ip);
    //To be used in test cases only
    //MacLearningEntryPtr TestGet(const MacLearningKey &key);
    bool RequestHandler(MacLearningEntryRequestPtr ptr);

    Agent* agent() {
        return agent_;
    }

    void Enqueue(MacLearningEntryRequestPtr req);
    void EnqueueMgmtReq(MacLearningEntryPtr ptr, bool add);

private:
    Agent *agent_;
    MacIpLearningEntryMap mac_ip_learning_entry_map_;
    MacIpLearningRequestQueue work_queue_;
    DISALLOW_COPY_AND_ASSIGN(MacIpLearningTable);
};
#endif
