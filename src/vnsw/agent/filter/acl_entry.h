/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_ACL_ENTRY_H__
#define __AGENT_ACL_ENTRY_H__

#include <boost/ptr_container/ptr_list.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/uuid/uuid.hpp>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

#include <agent_types.h>

struct PacketHeader;
class AclEntrySpec;
class TrafficAction;
class AclEntryMatch;

typedef std::vector<int32_t> AclEntryIDList;

class AclEntry {
public:
    enum AclType {
       TERMINAL = 1,
       NON_TERMINAL = 2,
    };

    typedef std::list<TrafficAction *> ActionList;
    static ActionList kEmptyActionList;
    AclEntry() : 
        id_(0), type_(TERMINAL), matches_(), actions_(), mirror_entry_(NULL),
        uuid_() {}

    AclEntry(AclType type) :
        id_(0), type_(type), matches_(), actions_(), mirror_entry_(NULL),
        uuid_() {}

    ~AclEntry();
    
    // Create the entry
    void PopulateAclEntry(const AclEntrySpec &acl_entry_spec);
    // Set Mirror ref
    void set_mirror_entry(MirrorEntryRef me);

    // Match packet header
    const ActionList &PacketMatch(const PacketHeader &packet_header) const;
    const ActionList &Actions() const {return actions_;};

    void SetAclEntrySandeshData(AclEntrySandeshData &data) const;

    bool IsTerminal() const;

    uint32_t id() const { return id_; }
    const std::string &uuid() const { return uuid_; }

    boost::intrusive::list_member_hook<> acl_list_node;

    bool operator==(const AclEntry &rhs) const;
private:
    uint32_t id_;
    AclType type_;
    std::vector<AclEntryMatch *> matches_;
    ActionList actions_;
    MirrorEntryRef mirror_entry_;
    std::string uuid_;

    DISALLOW_COPY_AND_ASSIGN(AclEntry);
};

#endif
