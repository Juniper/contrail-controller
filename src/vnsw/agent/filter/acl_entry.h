/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_ACL_ENTRY_H__
#define __AGENT_ACL_ENTRY_H__

#include <boost/ptr_container/ptr_list.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/uuid/uuid.hpp>
#include "base/util.h"
#include "net/address.h"
#include "filter/traffic_action.h"
#include  "filter/acl_entry_match.h"
#include "oper/agent_types.h"
#include "cmn/agent_cmn.h"

struct PacketHeader;
class AclEntrySpec;
typedef std::vector<int32_t> AclEntryIDList;

class AclEntry {
public:
    enum AclType {
       TERMINAL = 1,
       NON_TERMINAL = 2,
    };

    //typedef boost::ptr_list<TrafficAction> ActionList;
    typedef std::list<TrafficAction *> ActionList;
    static ActionList kEmptyActionList;
    AclEntry() : 
        id_(0), type_(TERMINAL), matches_(), actions_(), mirror_entry_(NULL) {};

    AclEntry(AclType type) :
        id_(0), type_(type), matches_(), actions_(), mirror_entry_(NULL) {};

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

    boost::intrusive::list_member_hook<> acl_list_node;

private:
    uint32_t id_;
    AclType type_;
    std::vector<AclEntryMatch *> matches_;
    ActionList actions_;
    MirrorEntryRef mirror_entry_;

    DISALLOW_COPY_AND_ASSIGN(AclEntry);
};

#endif
