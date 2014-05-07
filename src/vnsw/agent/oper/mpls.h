/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_hpp
#define vnsw_agent_mpls_hpp

#include <cmn/agent_cmn.h>
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

    MplsLabel(Type type, uint32_t label) :
        type_(type), label_(label), 
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

    static void CreateVlanNh(uint32_t label, const uuid &intf_uuid,
                             uint16_t tag);
    static void CreateVPortLabel(uint32_t label, const uuid &intf_uuid,
                                 bool policy, InterfaceNHFlags::Type type);
    static void CreateInetInterfaceLabel(uint32_t label,
                                           const string &ifname,
                                           bool policy,
                                           InterfaceNHFlags::Type type);
    static void CreateMcastLabelReq(const string &vrf_name, 
                                    const Ip4Address &grp_addr,
                                    const Ip4Address &src_addr, 
                                    uint32_t src_label, COMPOSITETYPE type);
    static void CreateEcmpLabel(uint32_t label, const std::string &vrf_name,
                                const Ip4Address &addr, uint8_t plen);
    static void DeleteMcastLabelReq(const string &vrf_name, 
                                    const Ip4Address &grp_addr,
                                    const Ip4Address &src_addr, 
                                    uint32_t src_label);
    // Delete MPLS Label entry
    static void DeleteReq(uint32_t label);
    static void Delete(uint32_t label);

    uint32_t GetRefCount() const {
        return AgentRefCount<MplsLabel>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void SendObjectLog(AgentLogEvent::type event) const;

private:
    Type type_;
    uint32_t label_;
    bool free_label_;
    NextHopRef nh_;
    friend class MplsTable;
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
                  InterfaceNHFlags::Type type) : 
        AgentData(), nh_key(new InterfaceNHKey(new InetInterfaceKey
                                               (intf_name), policy, type)) {
    }

    MplsLabelData(const uuid &intf_uuid, bool policy, 
                  InterfaceNHFlags::Type type) : 
        AgentData(), 
        nh_key(new InterfaceNHKey(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                     intf_uuid, ""), 
               policy, type)) {
    }

    MplsLabelData(const uuid &intf_uuid, int tag) : 
        AgentData(), nh_key(new VlanNHKey(intf_uuid, tag)) {
    }

    MplsLabelData(const string &vrf_name, const Ip4Address &grp_addr,
                  uint8_t plen, bool local_ecmp_nh) :
         AgentData(), 
         nh_key(new CompositeNHKey(vrf_name, grp_addr, plen, local_ecmp_nh)) {
         }

    //MplsLabelData(const NextHopKey &nh) : AgentData(), nh_key(nh) { };
    MplsLabelData(const string &vrf_name, const Ip4Address &grp_addr,
                  const Ip4Address &src_addr, bool local_ecmp_nh,
                  COMPOSITETYPE comp_type) :
        AgentData(), nh_key(new CompositeNHKey(vrf_name, grp_addr, src_addr,
                                               local_ecmp_nh, comp_type)) { 
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

    MplsTable(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~MplsTable() { }

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    // Allocate and Free label from the label_table
    uint32_t AllocLabel() {return label_table_.Insert(NULL);};
    void UpdateLabel(uint32_t label, MplsLabel *entry) {return label_table_.Update(label, entry);};
    void FreeLabel(uint32_t label) {label_table_.Remove(label);};
    MplsLabel *FindMplsLabel(size_t index) {return label_table_.At(index);};

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static MplsTable *GetInstance() {return mpls_table_;};
    void Process(DBRequest &req);
private:
    static MplsTable *mpls_table_;
    bool ChangeHandler(MplsLabel *mpls, const DBRequest *req);
    IndexVector<MplsLabel> label_table_;
    DISALLOW_COPY_AND_ASSIGN(MplsTable);
};

#endif // vnsw_agent_mpls_hpp
