/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_hpp
#define vnsw_agent_mpls_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/route_common.h>
#include <oper/nexthop.h>

using namespace boost::uuids;
using namespace std;

class MplsLabel : AgentRefCount<MplsLabel>, public AgentDBEntry {
public:
    enum Type {
        INVALID,
        VPORT_NH,
        MCAST_NH
    };
    typedef DependencyList<AgentRoute, MplsLabel> DependentPathList;

    MplsLabel(const Agent *agent, Type type, uint32_t label) :
        agent_(agent), type_(type), label_(label), 
        free_label_(false), nh_(NULL) { }
    virtual ~MplsLabel();

    bool IsLess(const DBEntry &rhs) const {
        const MplsLabel &mpls = static_cast<const MplsLabel &>(rhs);
        return label_ < mpls.label_;
    };
    virtual string ToString() const { return "MPLS"; };
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const;
    
    Type GetType() const {return type_;};
    uint32_t label() const {return label_;};
    const NextHop *nexthop() const {return nh_.get();};

    static void CreateVlanNh(const Agent *agent,
                             uint32_t label,
                             const uuid &intf_uuid,
                             uint16_t tag);
    static void CreateVPortLabel(const Agent *agent,
                                 uint32_t label,
                                 const uuid &intf_uuid,
                                 bool policy,
                                 InterfaceNHFlags::Type type,
                                 const MacAddress &mac);
    static void CreateInetInterfaceLabel(const Agent *agent,
                                         uint32_t label,
                                         const string &ifname,
                                         bool policy,
                                         InterfaceNHFlags::Type type,
                                         const MacAddress &mac);
    static void CreateEcmpLabel(const Agent *agent, uint32_t label,
                                COMPOSITETYPE type, bool policy,
                                ComponentNHKeyList &component_nh_key_list,
                                const std::string vrf_name);
    // Delete MPLS Label entry
    static void DeleteReq(const Agent *agent, uint32_t label);
    static void Delete(const Agent *agent, uint32_t label);

    uint32_t GetRefCount() const {
        return AgentRefCount<MplsLabel>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(const AgentDBTable *table,
                       AgentLogEvent::type event) const;
    void SyncDependentPath();
    bool IsFabricMulticastReservedLabel() const;

private:
    const Agent *agent_;
    Type type_;
    uint32_t label_;
    bool free_label_;
    NextHopRef nh_;
    friend class MplsTable;
    DEPENDENCY_LIST(AgentRoute, MplsLabel, mpls_label_);
    DISALLOW_COPY_AND_ASSIGN(MplsLabel);
};

class MplsLabelKey : public AgentKey {
public:
    MplsLabelKey(MplsLabel::Type type, uint32_t label) :
        AgentKey(), type_(type), label_(label) { };
    MplsLabelKey(uint32_t label) :
        AgentKey(), type_(MplsLabel::INVALID), label_(label) { };
    virtual ~MplsLabelKey() { };

    MplsLabel::Type type_;
    uint32_t label_;
};

class MplsLabelData : public AgentData {
public:
    MplsLabelData(const string &intf_name, bool policy,
                  InterfaceNHFlags::Type type, const MacAddress &mac) :
        AgentData(), nh_key(new InterfaceNHKey(new InetInterfaceKey
                                              (intf_name), policy, type, mac)) {
    }

    MplsLabelData(const uuid &intf_uuid, bool policy,
                  InterfaceNHFlags::Type type, const MacAddress &mac) :
        AgentData(),
        nh_key(new InterfaceNHKey(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                     intf_uuid, ""),
               policy, type, mac)) {
    }

    MplsLabelData(const uuid &intf_uuid, int tag) : 
        AgentData(), nh_key(new VlanNHKey(intf_uuid, tag)) {
    }

    MplsLabelData(COMPOSITETYPE type, bool policy,
        ComponentNHKeyList &component_nh_key_list, std::string vrf_name) :
        AgentData(), nh_key(new CompositeNHKey(type, policy,
        component_nh_key_list, vrf_name)) {
    }

    MplsLabelData(const std::string vrf_name, bool policy) :
        AgentData(), nh_key(new VrfNHKey(vrf_name, policy, false)) {
    }
    virtual ~MplsLabelData() { 
        if (nh_key) {
            delete nh_key;
        }
    };

    NextHopKey *nh_key;
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of MPLS Table
/////////////////////////////////////////////////////////////////////////////
class MplsTable : public AgentDBTable {
public:
    static const uint32_t kMaxLabelCount = 1000;
    static const uint32_t kInvalidLabel = 0xFFFFFFFF;
    static const uint32_t kStartLabel = 16;
    static const uint32_t kDpdkShiftBits = 4;

    MplsTable(DB *db, const std::string &name) : AgentDBTable(db, name),
        mpls_shift_bits_(0) { }
    virtual ~MplsTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual bool Delete(DBEntry *entry, const DBRequest *req);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    void CreateMcastLabel(uint32_t src_label,
                          COMPOSITETYPE type,
                          ComponentNHKeyList &component_nh_key_list,
                          const std::string vrf_name);
    void DeleteMcastLabel(uint32_t src_label);

    // Allocate and Free label from the label_table
    uint32_t AllocLabel() {
        uint32_t index = label_table_.Insert(NULL);
        return index << mpls_shift_bits_;
    }

    uint32_t InsertAtIndex(uint32_t label, MplsLabel *entry) {
        uint32_t index = label_table_.InsertAtIndex(label, entry);
        return index << mpls_shift_bits_;
    }

    void UpdateLabel(uint32_t label, MplsLabel *entry) {
        uint32_t index = label >> mpls_shift_bits_;
        return label_table_.Update(index, entry);
    }
    void FreeLabel(uint32_t label) {
        uint32_t index = label >> mpls_shift_bits_;
        label_table_.Remove(index);
    }
    MplsLabel *FindMplsLabel(size_t label) {
        uint32_t index = label >> mpls_shift_bits_;
        return label_table_.At(index);
    }

    static void CreateTableLabel(const Agent *agent, uint32_t label,
                                 const std::string &vrf_name,
                                 bool policy);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    void Process(DBRequest &req);
    bool ChangeNH(MplsLabel *mpls, NextHop *nh);

    void set_mpls_shift_bits(uint32_t shift) {
        mpls_shift_bits_ = shift;
    }
    void ReserveLabel(uint32_t start, uint32_t end);
    void ReserveMulticastLabel(uint32_t start, uint32_t end, uint8_t idx);
    bool IsFabricMulticastLabel(uint32_t label) const;

private:
    static MplsTable *mpls_table_;
    bool ChangeHandler(MplsLabel *mpls, const DBRequest *req);
    IndexVector<MplsLabel> label_table_;
    //When agent is running with dpdk vrouter, it may
    //use flow director functionality present in nic, to
    //forward the packet to right queue of VM based on mpls label.
    //NIC supporting that usually match 16 bits in packet and forward
    //that to a queue, in case of MPLS packet lower 4bits of mpls label
    //are present in one 16bit word and upper 16bit are present in
    //other word. Mpls label are allocated such that lower 4 bits are 0,
    //so that remaining MSB can uniquely identify the queue
    uint32_t mpls_shift_bits_;
    uint32_t multicast_label_start_[MAX_XMPP_SERVERS];
    uint32_t multicast_label_end_[MAX_XMPP_SERVERS];
    DISALLOW_COPY_AND_ASSIGN(MplsTable);
};

#endif // vnsw_agent_mpls_hpp
