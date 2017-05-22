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
struct FlowPolicyInfo;
class AclEntrySpec;
class TrafficAction;
class AclEntryMatch;

struct AclEntryID {

    AclEntryID(int32_t id) : reverse_(false) {
        id_ = integerToString(id);
    }

    AclEntryID(std::string id, bool reverse):
        id_(id), reverse_(false) {
    }

    bool operator ==(const AclEntryID &ace_id) const {
        if (id_ == ace_id.id_ && reverse_ == ace_id.reverse_) {
            return true;
        }
        return false;
    }

    bool operator <(const AclEntryID &ace_id) const {
        if (id_ != ace_id.id_) {
            return id_ < ace_id.id_;
        }

        return reverse_ < ace_id.reverse_;
    }

    bool operator >(const AclEntryID &ace_id) const {
      if (id_ != ace_id.id_) {
            return id_ > ace_id.id_;
        }

        return reverse_ > ace_id.reverse_;
    }

    bool operator !=(const AclEntryID &ace_id) const {
        if (id_ != ace_id.id_ ||
            reverse_ != ace_id.reverse_) {
            return true;
        }

        return false;
    }

    std::string id_;
    bool reverse_;
};

typedef std::vector<AclEntryID> AclEntryIDList;

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
    const ActionList &PacketMatch(const PacketHeader &packet_header,
                                  FlowPolicyInfo *info) const;
    const ActionList &Actions() const {return actions_;};

    void SetAclEntrySandeshData(AclEntrySandeshData &data) const;

    bool IsTerminal() const;

    const AclEntryID& id() const { return id_; }
    const std::string &uuid() const { return uuid_; }

    boost::intrusive::list_member_hook<> acl_list_node;

    bool operator==(const AclEntry &rhs) const;
    bool ResyncQosConfigEntries();
    bool IsQosConfigResolved();
    const AclEntryMatch* Get(uint32_t index) const {
        return matches_[index];
    }

private:
    AclEntryID id_;
    AclType type_;
    std::vector<AclEntryMatch *> matches_;
    ActionList actions_;
    MirrorEntryRef mirror_entry_;
    std::string uuid_;
    DISALLOW_COPY_AND_ASSIGN(AclEntry);
};

#endif
