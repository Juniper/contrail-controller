/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_POLICY_H__
#define __AGENT_POLICY_H__

#include <boost/uuid/uuid.hpp>
#include <vector>

typedef boost::uuids::uuid uuid;
struct PolicyConfigSpec;

struct PolicyKey : public DBRequestKey {
    PolicyKey(const uuid id) : id_(id) {} ;
    uuid id_;
};

struct PolicyData : public DBRequestData {
    PolicyData() {};
    ~PolicyData() {};
    void Init(const PolicyConfigSpec &policy_cfg);
    uuid vpc_id_;
    uuid policy_id_;
    std::string name_;
    bool inbound_;
    uuid acl_id_;
};

class Policy : public DBEntry {
public:
    typedef std::vector<AclPtr> AclPtrList;
    Policy(const uuid id);
    Policy() {};
    ~Policy();
    void Init(const PolicyData *req);
    bool AddAcl(const uuid acl_id) {return true;};
    bool IsLess(const DBEntry &rhs) const;
    KeyPtr GetDBRequestKey() const;
    void SetKey(const DBRequestKey *key);
    void SetName(const std::string str);
    std::string ToString() const;
    AclPtr FindAcl(const bool inbound, const uuid acl_id);
    void InsertAcl(const bool inbound, const AclPtr acl);
    void DeleteAcl(const bool inbound, const uuid acl_id);

    bool PacketMatch(const PacketHeader &packet_header,
                     const bool inbound,
                     AclEntry::ActionList &sal);

private:
    uuid vpc_id_;
    uuid policy_id_;
    std::string name_;
    AclPtrList inbound_acls_;
    AclPtrList outbound_acls_;
};

class PolicyTable : public DBTable {
public:
    PolicyTable(DB *db, const std::string &name) : DBTable(db, name) { };
    ~PolicyTable() { };
    
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *key) const;

    int Hash(const DBEntry *entry) const {return 0;};
    int Hash(const DBRequestKey *key) const {return 0;};

    DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    void Delete(DBEntry *entry, const DBRequest *req);

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void Register();

private:
    DISALLOW_COPY_AND_ASSIGN(PolicyTable);
};

#endif
