/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_assign_hpp
#define vnsw_agent_vrf_assign_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

/////////////////////////////////////////////////////////////////////////////
// Base class for VRF Assignment table. Implementation of specific types must
// derive from this class
/////////////////////////////////////////////////////////////////////////////

class VrfAssign : AgentRefCount<VrfAssign>, public AgentDBEntry {
public:
    enum Type {
        INVALID,
        VLAN
    };

    struct VrfAssignKey : public AgentKey {
        VrfAssignKey() : type_(INVALID) { };
        virtual ~VrfAssignKey() { };

        VrfAssign::Type type_;
        boost::uuids::uuid intf_uuid_;
        uint16_t vlan_tag_;

        void VlanInit(const boost::uuids::uuid intf_uuid, uint16_t vlan_tag) {
            type_ = VLAN;
            intf_uuid_ = intf_uuid;
            vlan_tag_ = vlan_tag;
        };
    };

    struct VrfAssignData : public AgentData {
        VrfAssignData(const std::string vrf_name) : 
            AgentData(), vrf_name_(vrf_name) { };
        virtual ~VrfAssignData() { };

        const std::string vrf_name_;
    };

    VrfAssign(Type type, Interface *interface)
        : type_(type), interface_(interface), vrf_(NULL) { };
    virtual ~VrfAssign() { };

    uint32_t GetRefCount() const {
        return AgentRefCount<VrfAssign>::GetRefCount();
    };
    bool IsLess(const DBEntry &rhs) const;
    bool Change(const DBRequest *req);

    virtual bool VrfAssignIsLess(const VrfAssign &rhs) const = 0;
    virtual bool VrfAssignChange(const DBRequest *req) {return false;};

    const Type GetType() const {return type_;};
    const Interface *GetInterface() const {return interface_.get();};
    const VrfEntry *GetVrf() const {return vrf_.get();};

protected:
    friend class VrfAssignTable;
    void SetVrf(VrfEntry *vrf) {vrf_ = vrf;};
    Type type_;
    InterfaceRef interface_;
    VrfEntryRef vrf_;
private:
    DISALLOW_COPY_AND_ASSIGN(VrfAssign);
};

/////////////////////////////////////////////////////////////////////////////
// Data for VLAN based VRF Assignment
/////////////////////////////////////////////////////////////////////////////
class VlanVrfAssign : public VrfAssign {
public:
    VlanVrfAssign(Interface *interface, uint16_t vlan_tag):
        VrfAssign(VLAN, interface), vlan_tag_(vlan_tag), nh_(NULL) {}
    virtual ~VlanVrfAssign() {}
    bool VrfAssignIsLess(const VrfAssign &rhs) const;

    virtual std::string ToString() const {return "VlanVrfAssign";};

    const uint32_t GetVlanTag() const {return vlan_tag_;};
    const NextHop *nh() const {return nh_.get();}
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    KeyPtr GetDBRequestKey() const;
    void SetKey(const DBRequestKey *key);
    virtual bool VrfAssignChange(const DBRequest *req);

private:
    uint16_t vlan_tag_;
    NextHopConstRef nh_;
    DISALLOW_COPY_AND_ASSIGN(VlanVrfAssign);
};

/////////////////////////////////////////////////////////////////////////////
// VRF Assignment table definition
/////////////////////////////////////////////////////////////////////////////
class VrfAssignTable : public AgentDBTable {
public:
    VrfAssignTable(DB *db, const std::string &name) : AgentDBTable(db, name) {}
    virtual ~VrfAssignTable() { }

    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static VrfAssignTable *GetInstance() {return vrf_assign_table_;};
    static void Shutdown();

    static Interface *FindInterface(const boost::uuids::uuid &intf_uuid);
    static VrfEntry *FindVrf(const std::string &name);

    static void CreateVlanReq(const boost::uuids::uuid &intf_uuid,
            const std::string &vrf_name, uint16_t vlan_tag);
    static void DeleteVlanReq(const boost::uuids::uuid &intf_uuid,
                              uint16_t vlan_tag);
    static VrfAssign *FindVlanReq(const boost::uuids::uuid &intf_uuid,
                                  uint16_t vlan_tag);

private:
    static VrfAssignTable *vrf_assign_table_;
    VrfAssign *AllocWithKey(const DBRequestKey *k) const;
    DISALLOW_COPY_AND_ASSIGN(VrfAssignTable);
};

#endif // vnsw_agent_vrf_assign_hpp
