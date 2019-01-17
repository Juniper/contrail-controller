/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_TAG_HPP_
#define SRC_VNSW_AGENT_TAG_HPP_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <oper/oper_db.h>

// Tag helps in attaching well defined attribute to any object
// in system. Currently tag attached to VMI, VM, VN and Project are of
// significance
//
// Agent builds tag list directly at VMI.
// Tag built at VMI includes all the tags that are defined at VMI, VM,
// VN and project.
//
// Derivation of tag at VMI:
// VMI config processing goes thru below links to build tag list
// VMI --> Tag
// VMI --> VM --> Tag
// VMI --> VN --> Tag
// VMI --> Project --> Tag
//
//Label can be multiple at VMI.
// VMI will have only one each tag of type application, site,
// deployment and tier, and priority would be in above order of link
// Tag list built at VMI would be  exported to control-node.
// Tag list would be used in ACL lookups
struct TagKey : public AgentOperDBKey {
    TagKey(boost::uuids::uuid tag_uuid) :
        AgentOperDBKey(), tag_uuid_(tag_uuid) {}
    virtual ~TagKey() {}

    boost::uuids::uuid tag_uuid_;
};

struct TagData : public AgentOperDBData {
    typedef std::vector<boost::uuids::uuid> PolicySetUuidList;
    TagData(Agent *agent, IFMapNode *node, const uint32_t &tag_id):
            AgentOperDBData(agent, node), tag_id_(tag_id){
    }
    virtual ~TagData() {}

    uint32_t tag_id_;
    std::string name_;
    std::string tag_value_;
    uint32_t tag_type_;
    PolicySetUuidList policy_set_uuid_list_;
};

class TagEntry : AgentRefCount<TagEntry>, public AgentOperDBEntry {
public:
    enum {
        LABEL = 0,
        APPLICATION = 1,
        TIER = 2,
        DEPLOYMENT = 3,
        SITE = 4,
        NEUTRON_FWAAS = 5,
        INVALID = 0xFFFFFFFF
    };

    typedef std::map<uint32_t, std::string> TagIdNameMap;
    typedef std::pair<uint32_t, std::string> TagIdNamePair;
    typedef std::vector<PolicySetRef> PolicySetList;

    static const uint32_t kInvalidTagId = 0xFFFFFFFF;
    static const uint32_t kTagTypeBitShift = 16;
    static const std::map<uint32_t, std::string> TagTypeStr;

    TagEntry(const boost::uuids::uuid tag_uuid) :
        tag_uuid_(tag_uuid), tag_id_(kInvalidTagId) {}
    virtual ~TagEntry() {}

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;
    bool Change(TagTable *table, const DBRequest *req);

    uint32_t GetRefCount() const {
        return AgentRefCount<TagEntry>::GetRefCount();
    }
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    const boost::uuids::uuid& tag_uuid() const {return tag_uuid_;}
    const uint32_t& tag_id() const {return tag_id_;}

    const PolicySetList& policy_set_list() const {
        return policy_set_list_;
    }

    bool IsApplicationTag() const;

    bool IsNeutronFwaasTag() const;

    const std::string& name() const {
        return name_;
    }

    uint32_t tag_type() const {
        return tag_type_;
    }

    const std::string& tag_value() const {
        return tag_value_;
    }

    static const std::string& GetTypeStr(uint32_t tag_type);
    static uint32_t GetTypeVal(const std::string &str,
                               const std::string &val);

private:
    friend class TagTable;
    boost::uuids::uuid tag_uuid_;
    uint32_t tag_id_;
    uint32_t tag_type_;
    std::string tag_value_;
    std::string name_;
    PolicySetList policy_set_list_;
    DISALLOW_COPY_AND_ASSIGN(TagEntry);
};

class TagTable : public AgentOperDBTable {
public:
    enum {
        LABEL = 0,
        APPLICATION = 1,
        TIER = 2,
        DEPLOYMENT = 3,
        SITE = 4,
        NEUTRON_FWAAS = 5,
        INVALID = 0xFFFFFFFF
    };

    typedef std::map<uint32_t, std::string> TagIdNameMap;
    typedef std::pair<uint32_t, std::string> TagIdNamePair;

    TagTable(DB *db, const std::string &name) : AgentOperDBTable(db, name) {}
    virtual ~TagTable() {}

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t  Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *OperDBAdd(const DBRequest *req);
    virtual bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    virtual bool OperDBDelete(DBEntry *entry, const DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    virtual bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
                       const boost::uuids::uuid &u);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

    const std::string& TagName(uint32_t id) {
        TagIdNameMap::const_iterator it = id_name_map_.find(id);
        if (it != id_name_map_.end()) {
            return it->second;
        }

        return Agent::NullString();
    }

    void Insert(uint32_t tag_id, const std::string &name) {
        id_name_map_.insert(TagIdNamePair(tag_id, name));
    }

    void Erase(uint32_t tag_id) {
        id_name_map_.erase(tag_id);
    }

    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    bool IsGlobalAps(Agent *agent, IFMapNode *node);
    TagData* BuildData(Agent *agent, IFMapNode *node);

private:
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    TagIdNameMap id_name_map_;
    DISALLOW_COPY_AND_ASSIGN(TagTable);
};

#endif // vnsw_agent_tag.hpp
