/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_TAG_HPP_
#define SRC_VNSW_AGENT_TAG_HPP_

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <oper/oper_db.h>

struct TagKey : public AgentOperDBKey {
    TagKey(uuid tag_uuid) : AgentOperDBKey(), tag_uuid_(tag_uuid) {} ;
    virtual ~TagKey() { };

    uuid tag_uuid_;
};

struct TagData : public AgentOperDBData {
    TagData(Agent *agent, IFMapNode *node, const uint32_t &tag_id):
                   AgentOperDBData(agent, node), tag_id_(tag_id){
    }
    virtual ~TagData() { }

    uint32_t tag_id_;
    boost::uuids::uuid policy_set_;
    std::string name_;
    uint32_t tag_type_;
    std::string tag_value_;
};

class TagEntry : AgentRefCount<TagEntry>, public AgentOperDBEntry {
public:
    TagEntry(uuid tag_uuid, uint32_t tag_id) : tag_uuid_(tag_uuid), tag_id_(tag_id) {};
    TagEntry(uuid tag_uuid) : tag_uuid_(tag_uuid) { };
    virtual ~TagEntry() { };

    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual string ToString() const;

    const boost::uuids::uuid& tag_uuid() const {return tag_uuid_;};
    const uint32_t& tag_id() const {return tag_id_;}

    uint32_t GetRefCount() const {
        return AgentRefCount<TagEntry>::GetRefCount();
    }
    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    PolicySet* policy_set() {
        return policy_set_.get();
    }

    bool IsApplicationTag() const;

    const std::string& name() const {
        return name_;
    }

    uint32_t tag_type() const {
        return tag_type_;
    }

    const std::string& tag_value() const {
        return tag_value_;
    }

private:
    friend class TagTable;
    boost::uuids::uuid tag_uuid_;
    uint32_t tag_id_;
    uint32_t tag_type_;
    std::string tag_value_;
    PolicySetRef policy_set_;
    std::string name_;
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
        INVALID = 0xFFFFFFFF
    };

    typedef std::map<uint32_t, std::string> TagIdNameMap;
    typedef std::pair<uint32_t, std::string> TagIdNamePair;

    static const uint32_t kInvalidTagId = 0;
    static const uint32_t kTagTypeBitShift = 27;
    static const std::map<uint32_t, std::string> TagTypeStr;

    TagTable(DB *db, const std::string &name) : AgentOperDBTable(db, name) { }
    virtual ~TagTable() { }

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
    static TagTable *GetInstance() {return tag_table_;};

    const std::string& TagName(uint32_t id) {
        TagIdNameMap::const_iterator it = id_name_map_.find(id);
        if (it != id_name_map_.end()) {
            return it->second;
        }

        return Agent::NullString();
    }

    static const std::string& GetTypeStr(uint32_t tag_type);
    static uint32_t GetTypeVal(const std::string &str);

private:
    static TagTable* tag_table_;
    bool ChangeHandler(DBEntry *entry, const DBRequest *req);
    TagIdNameMap id_name_map_;
    DISALLOW_COPY_AND_ASSIGN(TagTable);
};

#endif // vnsw_agent_tag.hpp
