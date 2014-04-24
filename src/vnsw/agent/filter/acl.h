/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_ACL_N_H__
#define __AGENT_ACL_N_H__

#include "filter/traffic_action.h"
#include <filter/acl_entry_match.h>
#include "filter/acl_entry_spec.h"
#include "filter/acl_entry.h"

#include <boost/intrusive/list.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/intrusive_ptr.hpp>
#include <tbb/atomic.h>
#include <cmn/agent_cmn.h>
#include <oper/agent_types.h>

struct FlowKey;

using namespace boost::uuids;
class VnEntry;
class Interface;

struct FlowAction {
    FlowAction(): 
        action(0), mirror_l() {};
    ~FlowAction() { };

    void Clear() {
        action = 0;
        mirror_l.clear();
    };

    uint32_t action;
    std::vector<MirrorActionSpec> mirror_l;
    VrfTranslateActionSpec vrf_translate_action_;
};

struct MatchAclParams {
    MatchAclParams(): acl(NULL), ace_id_list(), terminal_rule(false) {};
    ~MatchAclParams() { };

    AclDBEntryConstRef acl;
    AclEntryIDList ace_id_list;
    FlowAction action_info;
    bool terminal_rule;
};

struct AclKey : public AgentKey {
    AclKey(uuid id) : AgentKey(), uuid_(id) {} ;
    virtual ~AclKey() {};

    uuid uuid_;    
};

struct AclData: public AgentData {
    AclData(AclSpec &aclspec) : AgentData(), ace_id_to_del_(0), ace_add(false), acl_spec_(aclspec) { };
    AclData(int ace_id_to_del) : AgentData(), ace_id_to_del_(ace_id_to_del) { };
    virtual ~AclData() { };

    // Delete a particular ace
    int ace_id_to_del_;
    // true: add to existing aces, false:replace existing aces with specified in the spec
    bool ace_add;
    std::string cfg_name_;
    AclSpec acl_spec_;
};

class AclDBEntry : AgentRefCount<AclDBEntry>, public AgentDBEntry {
public:
    typedef boost::intrusive::member_hook<AclEntry,
            boost::intrusive::list_member_hook<>, 
            &AclEntry::acl_list_node> AclEntryNode;
    typedef boost::intrusive::list<AclEntry, AclEntryNode> AclEntries;
    
    AclDBEntry(uuid id) : uuid_(id), dynamic_acl_(false) { };
    ~AclDBEntry() { };

    bool IsLess(const DBEntry &rhs) const;
    KeyPtr GetDBRequestKey() const;
    void SetKey(const DBRequestKey *key);
    std::string ToString() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<AclDBEntry>::GetRefCount();
    }
    const uuid &GetUuid() const {return uuid_;};
    const std::string &GetName() const {return name_;};
    void SetName(const std::string name) {name_ = name;};
    bool DBEntrySandesh(Sandesh *resp, std::string &name) const;
    void SetAclSandeshData(AclSandeshData &data) const;

    // ACL methods
    //AclEntry *AddAclEntry(const AclEntrySpec &acl_entry_spec);
    AclEntry *AddAclEntry(const AclEntrySpec &acl_entry_spec, AclEntries &entries);
    bool DeleteAclEntry(const uint32_t acl_entry_id);
    void DeleteAllAclEntries();
    uint32_t Size() const {return acl_entries_.size();};
    void SetAclEntries(AclEntries &entries);
    void SetDynamicAcl(bool dyn) {dynamic_acl_ = dyn;};
    bool GetDynamicAcl () const {return dynamic_acl_;};

    // Packet Match
    bool PacketMatch(const PacketHeader &packet_header, 
		     MatchAclParams &m_acl) const;
private:
    friend class AclTable;
    uuid uuid_;
    bool dynamic_acl_;
    std::string name_;
    AclEntries acl_entries_;
    DISALLOW_COPY_AND_ASSIGN(AclDBEntry);
};

class AclTable : public AgentDBTable {
public:
    typedef std::map<std::string, TrafficAction::Action> TrafficActionMap;
    AclTable(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~AclTable() { }
    void GetTables(DB *db) { };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    TrafficAction::Action ConvertActionString(std::string action) const;
    static void AclFlowResponse(const std::string acl_uuid_str, 
                                const std::string ctx, const int last_count);
    static void AclFlowCountResponse(const std::string acl_uuid_str, 
                                     const std::string ctx, int ace_id);
private:
    static const AclDBEntry* GetAclDBEntry(const std::string uuid_str, 
                                           const std::string ctx,
                                           SandeshResponse *resp);
    void ActionInit();
    TrafficActionMap ta_map_;
    DISALLOW_COPY_AND_ASSIGN(AclTable);
};

extern SandeshTraceBufferPtr AclTraceBuf;

#define ACL_TRACE(obj, ...)\
do {\
    Acl##obj::TraceMsg(AclTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(false);\


#endif
