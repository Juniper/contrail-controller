/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_H_

#include "cmn/agent.h"
#include "mac_learning_key.h"
class MacEntryResp;
/*
 * High level mac learning modules
 *
 * MacLearningProto:
 * Processes the packet trapped for MAC learning, based on the hash
 * of the VRF and MAC particular MAC Learning parition would be chosen
 * for further processing
 *
 * MacLearningPartition:
 * A logical context for processing MAC learning requests in parallel
 * This modules learns the MAC entry and
 * 1> Enqueues a request for route add
 * 2> Enqueues a request to MacLearningMgmt for dependency tracking
 * 3> Enqueues a request to MacAgingPartiton for aging
 *
 * MacLearningMgmt:
 * This modules builds a dependency tracking for a MAC entry.
 * MAC entry would be dependent on interface, VRF and
 * a route(in case of PBB tunnel learnt MAC). So given a DB object
 * it would have a list of all the MAC learnt on that object so that
 * any change can result in resync of MAC entry.
 *
 * MacLearningDBClient:
 * Listens for change on DB object and enqueues a request to MacLearningMgmt
 * for revaluation of MAC entry depenedent on this DB object.
 * Current DB tables of intereset inclue:
 * 1> Interface (For local learnt mac address)
 * 2> Route (For mac learnt over PBB)
 * 3> VRF
 *
 * MacAgingPatition:
 * For each MacLearningPartition there will be MacAgingPartition, which
 * maintains a per VRF list of MAC entries, upon timer expiry based on
 * aging timeout configured on VRF and no. of entries in VRF no. of entries
 * would be visited for stats and aged if no activity is seen on the entry.
 *
 *                       ++++++++++++++++++++
 *                       +   Mac Aging X    +
 *                       ++++++++++++++++++++
 *                                |
 *                                |
 *                                |
 *                      ++++++++++++++++++++++++
 *                   -->+ MacLearningPartitionX+--|
 *                   |  ++++++++++++++++++++++++  |
 * +++++++++++++++   |                            | N:1 +++++++++++++++++++
 * +Packet Proto +-->|                            |<--->+ MacLearningMgmt +
 * +++++++++++++++   |                            |     +++++++++++++++++++
 *                   |  ++++++++++++++++++++++++  |            ^
 *                   -->+ MacLearningPartitionY+--|            |
 *                      ++++++++++++++++++++++++               |
 *                                 |                      ++++++++++++++++
 *                                 |1:1                   + MacLearning  +
 *                                 |                      + DB Client    +
 *                       ++++++++++++++++++++             ++++++++++++++++
 *                       +    Mac Aging Y   +
 *                       ++++++++++++++++++++
 */

class MacLearningPartition;
class MacAgingTable;
class MacAgingPartition;
class MacLearningEntry {
public:
    MacLearningEntry(MacLearningPartition *table, uint32_t vrf_id,
                     const MacAddress &mac, uint32_t index);
    virtual ~MacLearningEntry() {}
    virtual void Add() = 0;
    virtual void Delete();
    virtual void Resync() { Add(); }

    MacLearningPartition* mac_learning_table() const {
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
     virtual ~MacLearningEntryLocal() {}

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
    virtual ~MacLearningEntryRemote() {}

    void Add();

    const IpAddress& remote_ip() const {
        return remote_ip_;
    }
private:
    IpAddress remote_ip_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntryRemote);
};

//Structure used to hold MAC entry learned on
//PBB tunnel packet
class MacLearningEntryPBB : public MacLearningEntry {
public:
    MacLearningEntryPBB(MacLearningPartition *table, uint32_t vrf_id,
                        const MacAddress &mac, uint32_t index,
                        const MacAddress &bmac);
    virtual ~MacLearningEntryPBB() {}

    void Add();

    const MacAddress& bmac() const {
        return bmac_;
    }

private:
    const MacAddress bmac_;
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
typedef WorkQueue<MacLearningEntryRequestPtr> MacLearningRequestQueue;

//Mac learning Parition holds all the mac entries hashed
//based on VRF + MAC, and corresponding to each partition
//there will be a aging partition holding all the MAC entries
//present in this partiton
class MacLearningPartition {
public:
    typedef std::pair<MacLearningKey,
                      MacLearningEntryPtr> MacLearningEntryPair;
    typedef std::map<MacLearningKey,
                     MacLearningEntryPtr,
                     MacLearningKeyCmp> MacLearningEntryTable;

    MacLearningPartition(Agent *agent, uint32_t id);
    virtual ~MacLearningPartition();
    void Add(MacLearningEntryPtr ptr);
    void Resync(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);
    void DeleteAll();
    bool RequestHandler(MacLearningEntryRequestPtr ptr);

    Agent* agent() {
        return agent_;
    }

    MacAgingPartition* aging_partition() const {
        return aging_partition_.get();
    }

    void Enqueue(MacLearningEntryRequestPtr req);
    void EnqueueMgmtReq(MacLearningEntryPtr ptr, bool add);
private:
    friend class MacLearningSandeshResp;
    Agent *agent_;
    uint32_t id_;
    MacLearningEntryTable mac_learning_table_;
    MacLearningRequestQueue request_queue_;
    boost::shared_ptr<MacAgingPartition> aging_partition_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningPartition);
};

class MacLearningSandeshResp : public Task {
public:
    const static uint32_t kMaxResponse = 100;
    const static char kDelimiter = '-';
    MacLearningSandeshResp(Agent *agent, MacEntryResp *resp,
                        std::string resp_ctx,
                        std::string key,
                        const MacAddress &mac);
    virtual ~MacLearningSandeshResp();
    std::string Description() const { return "MacLearningSandeshRespTask"; }

private:
    bool Run();
    bool SetMacKey(string key);
    void SendResponse(SandeshResponse *resp);

    const MacLearningPartition* GetPartition();
    std::string GetMacKey();

    Agent *agent_;
    MacEntryResp *resp_;
    std::string resp_data_;
    uint32_t   partition_id_;
    uint32_t   vrf_id_;
    MacAddress mac_;
    bool       exact_match_;
    MacAddress user_given_mac_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningSandeshResp);
};
#endif
