/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_hpp
#define vnsw_agent_mpls_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/route_common.h>
#include <oper/nexthop.h>
#include <resource_manager/resource_manager.h>

extern SandeshTraceBufferPtr MplsTraceBuf;
#define MPLS_TRACE(obj, ...)                                                  \
do {                                                                          \
    obj::TraceMsg(MplsTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);           \
} while (false)

class MplsLabelKey : public AgentKey {
public:
    MplsLabelKey(uint32_t label) : AgentKey(), label_(label) { }
    virtual ~MplsLabelKey() { }

    uint32_t label() const { return label_; }
private:
    uint32_t label_;
};

class MplsLabelData : public AgentData {
public:
    MplsLabelData(const NextHopKey *nh_key) : nh_key_(nh_key) {
    }

    virtual ~MplsLabelData() {
    }

    const NextHopKey *nh_key() const { return nh_key_.get(); }
private:
    std::auto_ptr<const NextHopKey> nh_key_;
};

/////////////////////////////////////////////////////////////////////////////
// MplsLabel entry
/////////////////////////////////////////////////////////////////////////////
class MplsLabel : AgentRefCount<MplsLabel>, public AgentDBEntry {
public:
    typedef DependencyList<AgentRoute, MplsLabel> DependentPathList;

    MplsLabel(uint32_t label);
    virtual ~MplsLabel();

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual std::string ToString() const;

    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    
    virtual uint32_t GetRefCount() const;
    virtual bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    void Add(Agent *agent, const DBRequest *req);
    bool Change(const DBRequest *req);
    void Delete(const DBRequest *req);
    bool ChangeInternal(Agent *agent, const DBRequest *req);
    bool ChangeNH(NextHop *nh);

    void SyncDependentPath();
    bool IsFabricMulticastReservedLabel() const;
    void SendObjectLog(const AgentDBTable *table,
                       AgentLogEvent::type event) const;

    uint32_t label() const {return label_;}
    const NextHop *nexthop() const {return nh_;}
private:
    friend class MplsTable;
    uint32_t label_;
    NextHop *nh_;
    DEPENDENCY_LIST(AgentRoute, MplsLabel, mpls_label_);
    DISALLOW_COPY_AND_ASSIGN(MplsLabel);
};

/////////////////////////////////////////////////////////////////////////////
// MPLS Table
/////////////////////////////////////////////////////////////////////////////
class MplsTable : public AgentDBTable {
public:
    static const uint32_t kInvalidLabel = 0xFFFFFFFF;
    static const uint32_t kStartLabel = 16;
    static const uint32_t kDpdkShiftBits = 4;

    MplsTable(DB *db, const std::string &name);
    virtual ~MplsTable();

    virtual void Process(DBRequest &req);
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);
    virtual void OnZeroRefcount(AgentDBEntry *e);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    uint32_t CreateRouteLabel(uint32_t label, const NextHopKey *nh_key,
                              const std::string &vrf_name,
                              const std::string &route);
    void ReserveLabel(uint32_t start, uint32_t end);
    void FreeReserveLabel(uint32_t start, uint32_t end);
    MplsLabel *AllocLabel(const NextHopKey *nh_key);
    uint32_t AllocLabel(ResourceManager::KeyPtr key);
    void FreeLabel(uint32_t label);

    void ReserveMulticastLabel(uint32_t start, uint32_t end, uint8_t idx);
    bool IsFabricMulticastLabel(uint32_t label) const;
    MplsLabel *FindMplsLabel(uint32_t label);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
private:
    bool ChangeHandler(MplsLabel *mpls, const DBRequest *req);
    IndexVector<MplsLabel *> label_table_;
    uint32_t multicast_label_start_[MAX_XMPP_SERVERS];
    uint32_t multicast_label_end_[MAX_XMPP_SERVERS];
    DISALLOW_COPY_AND_ASSIGN(MplsTable);
};

#endif // vnsw_agent_mpls_hpp
