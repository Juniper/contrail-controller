/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_H_

#include "cmn/agent.h"
#include "mac_learning_key.h"
#include "mac_learning_base.h"
#include "mac_learning_event.h"
#include "pkt/flow_token.h"
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

class MacPbbLearningEntry : public MacLearningEntry {
public:
    typedef std::vector<TokenPtr> TokenList;
    MacPbbLearningEntry(MacLearningPartition *table, uint32_t vrf_id,
                     const MacAddress &mac, uint32_t index);
    virtual ~MacPbbLearningEntry() {}
    virtual bool Add() = 0;
    virtual void Delete();
    virtual void Resync();
    virtual void AddWithToken();

    MacLearningPartition* mac_learning_table() const {
        return mac_learning_table_;
    }

    uint32_t index() const {
        return index_;
    }

    const MacAddress& mac() const {
        return key_.mac_;
    }

    uint32_t vrf_id() {
        return key_.vrf_id_;
    }

    const MacLearningKey& key() const {
        return key_;
    }

    void AddToken(TokenPtr ptr) {
        tbb::mutex::scoped_lock lock(mutex_);
        list_.push_back(ptr);
    }

    void ReleaseToken() {
        tbb::mutex::scoped_lock lock(mutex_);
        list_.clear();
    }

    void CopyToken(MacLearningEntry *entry) {
        MacPbbLearningEntry *entry1 =
            dynamic_cast<MacPbbLearningEntry *>(entry);
        tbb::mutex::scoped_lock lock(mutex_);
        tbb::mutex::scoped_lock lock2(entry1->mutex_);
        list_ = entry1->list_;
        entry1->list_.clear();
    }

    bool HasTokens() {
        tbb::mutex::scoped_lock lock(mutex_);
        return list_.size();
    }
    void EnqueueToTable(MacLearningEntryRequestPtr req);

protected:
    MacLearningPartition *mac_learning_table_;
    MacLearningKey key_;
    uint32_t index_;
    uint32_t ethernet_tag_;
    TokenList list_;
    tbb::mutex mutex_;
private:
    DISALLOW_COPY_AND_ASSIGN(MacPbbLearningEntry);
};
//Structure used to hold MAC entry learned
//on local interface
class MacLearningEntryLocal : public MacPbbLearningEntry {
public:
     MacLearningEntryLocal(MacLearningPartition *table, uint32_t vrf_id,
                           const MacAddress &mac, uint32_t index,
                           InterfaceConstRef intf);
     virtual ~MacLearningEntryLocal() {}

     bool Add();

     const Interface* intf() {
         return intf_.get();
     }

private:
   InterfaceConstRef intf_;
   DISALLOW_COPY_AND_ASSIGN(MacLearningEntryLocal);
};

//Structure used to hold MAC entry learned on
//tunnel packet i.e vxlan scenario for now
class MacLearningEntryRemote : public MacPbbLearningEntry {
public:
    MacLearningEntryRemote(MacLearningPartition *table, uint32_t vrf_id,
                           const MacAddress &mac, uint32_t index,
                           const IpAddress remote_ip);
    virtual ~MacLearningEntryRemote() {}

    bool Add();

    const IpAddress& remote_ip() const {
        return remote_ip_;
    }
private:
    IpAddress remote_ip_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntryRemote);
};

//Structure used to hold MAC entry learned on
//PBB tunnel packet
class MacLearningEntryPBB : public MacPbbLearningEntry {
public:
    MacLearningEntryPBB(MacLearningPartition *table, uint32_t vrf_id,
                        const MacAddress &mac, uint32_t index,
                        const MacAddress &bmac);
    virtual ~MacLearningEntryPBB() {}

    bool Add();

    const MacAddress& bmac() const {
        return bmac_;
    }

private:
    const MacAddress bmac_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntryPBB);
};

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

    MacLearningPartition(Agent *agent, MacLearningProto *proto,
                         uint32_t id);
    virtual ~MacLearningPartition();
    void Add(MacLearningEntryPtr ptr);
    void Resync(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);
    void DeleteAll();
    void ReleaseToken(const MacLearningKey &key);
    MacLearningEntry* Find(const MacLearningKey &key);
    //To be used in test cases only
    MacLearningEntryPtr TestGet(const MacLearningKey &key);
    bool RequestHandler(MacLearningEntryRequestPtr ptr);

    Agent* agent() {
        return agent_;
    }

    MacAgingPartition* aging_partition() const {
        return aging_partition_.get();
    }

    uint32_t id() const {
        return id_;
    }

    void Enqueue(MacLearningEntryRequestPtr req);
    void EnqueueMgmtReq(MacLearningEntryPtr ptr, bool add);
    void MayBeStartRunner(TokenPool *pool);

    void SetDeleteQueueDisable(bool disable) {
        delete_request_queue_.SetQueueDisable(disable);
    }

private:
    friend class MacLearningSandeshResp;
    Agent *agent_;
    uint32_t id_;
    MacLearningEntryTable mac_learning_table_;
    MacLearningRequestQueue add_request_queue_;
    MacLearningRequestQueue change_request_queue_;
    MacLearningRequestQueue delete_request_queue_;
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
