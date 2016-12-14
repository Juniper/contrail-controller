/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_H_

#include "cmn/agent.h"
#include "mac_learning_key.h"

class MacLearningPartition;
class MacAgingTable;
class MacAgingPartition;
class MacLearningEntry {
public:
    MacLearningEntry(MacLearningPartition *table, uint32_t vrf_id,
                     const MacAddress &mac, uint32_t index);
    virtual void Add() = 0;
    virtual void Delete();
    virtual void Resync() { Add(); }

    MacLearningPartition* mac_learning_table() {
        return mac_learning_table_;
    }

    uint32_t index() const {
        return index_;
    }

    const MacAddress& mac() const {
        return key_.mac_;
    }

    VrfEntry* vrf() const {
        return vrf_.get();
    }

    uint32_t vrf_id() const {
        return key_.vrf_id_;
    }

    const MacLearningKey& key() const {
        return key_;
    }

protected:
    MacLearningPartition *mac_learning_table_;
    MacLearningKey key_;
    uint32_t index_;
    uint32_t ethernet_tag_;
    VrfEntryRef vrf_;
private:
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntry);
};

//Structure used to hold MAC entry learned
//on local interface
class MacLearningEntryLocal : public MacLearningEntry {
public:
     MacLearningEntryLocal(MacLearningPartition *table, uint32_t vrf_id,
                           const MacAddress &mac, uint32_t index,
                           InterfaceConstRef intf);
     ~MacLearningEntryLocal() {}

     void Add();

     const Interface* intf() {
         return intf_.get();
     }

private:
   InterfaceConstRef intf_;
   DISALLOW_COPY_AND_ASSIGN(MacLearningEntryLocal);
};

//Structure used to hold MAC entry learned on
//tunnel packet i.e vxlan scenario for now
class MacLearningEntryRemote : public MacLearningEntry {
public:
    MacLearningEntryRemote(MacLearningPartition *table, uint32_t vrf_id,
                           const MacAddress &mac, uint32_t index,
                           const IpAddress remote_ip);
    ~MacLearningEntryRemote() {}

    void Add();

    const IpAddress& remote_ip() const {
        return remote_ip_;
    }
private:
    IpAddress remote_ip_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntryRemote);
};

//Structure used to hold MAC entry learned on
//tunnel packet i.e vxlan scenario for now
class MacLearningEntryPBB : public MacLearningEntry {
public:
    MacLearningEntryPBB(MacLearningPartition *table, uint32_t vrf_id,
                        const MacAddress &mac, uint32_t index,
                        const std::string &evpn_vrf,
                        const MacAddress &bmac, uint32_t isid);
    ~MacLearningEntryPBB() {}

    void Add();

    const MacAddress& bmac() const {
        return bmac_;
    }

    const std::string evpn_vrf() const {
        return evpn_vrf_;
    }
private:
    const std::string evpn_vrf_;
    const MacAddress bmac_;
    uint32_t isid_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntryPBB);
};

typedef boost::shared_ptr<MacLearningEntry> MacLearningEntryPtr;

class MacLearningEntryRequest {
public:
     enum Event {
         INVALID,
         VROUTER_MSG,
         ADD_MAC,
         RESYNC_MAC,
         DELETE_MAC,
         FREE_DB_ENTRY,
         DELETE_VRF
     };

     MacLearningEntryRequest(Event event, PktInfoPtr pkt):
         event_(event), pkt_info_(pkt) {}
     MacLearningEntryRequest(Event event, MacLearningEntryPtr ptr):
         event_(event), mac_learning_entry_(ptr) {
     }

     MacLearningEntryRequest(Event event, uint32_t vrf_id):
         event_(event), vrf_id_(vrf_id) {
     }

     MacLearningEntryRequest(Event event, const DBEntry *entry):
         event_(event), db_entry_(entry) {}

     MacLearningEntryPtr mac_learning_entry() {
         return mac_learning_entry_;
     }

     Event event() {
         return event_;
     }

     const DBEntry *db_entry() {
         return db_entry_;
     }

     PktInfoPtr pkt_info() {
         return pkt_info_;
     }

     uint32_t vrf_id() {
         return vrf_id_;
     }

private:
    Event event_;
    MacLearningEntryPtr mac_learning_entry_;
    PktInfoPtr pkt_info_;
    uint32_t vrf_id_;
    const DBEntry *db_entry_;
};
typedef boost::shared_ptr<MacLearningEntryRequest> MacLearningEntryRequestPtr;

//Per VRF table for MAC learning
class MacLearningPartition {
public:
    typedef std::pair<MacLearningKey,
                      MacLearningEntryPtr> MacLearningEntryPair;
    typedef std::map<MacLearningKey,
                     MacLearningEntryPtr,
                     MacLearningKeyCmp> MacLearningEntryTable;
    typedef WorkQueue<MacLearningEntryRequestPtr> MacLearningRequestQueue;

    MacLearningPartition(Agent *agent, uint32_t id);
    ~MacLearningPartition();
    void Add(MacLearningEntryPtr ptr);
    void Resync(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);
    void DeleteAll();
    bool RequestHandler(MacLearningEntryRequestPtr ptr);

    Agent* agent() {
        return agent_;
    }

    MacAgingPartition* aging_partition() {
        return aging_partition_.get();
    }

    void Enqueue(MacLearningEntryRequestPtr req);
    void EnqueueMgmtReq(MacLearningEntryPtr ptr, bool add);
private:
    Agent *agent_;
    uint32_t id_;
    MacLearningEntryTable mac_learning_table_;
    MacLearningRequestQueue request_queue_;
    boost::shared_ptr<MacAgingPartition> aging_partition_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningPartition);
};
#endif
