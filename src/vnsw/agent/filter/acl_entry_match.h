/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_ACL_ENTRY_MATCH_H__
#define __AGENT_ACL_ENTRY_MATCH_H__

#include <boost/ptr_container/ptr_list.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/uuid/uuid.hpp>
#include "base/util.h"
#include "net/address.h"
#include "vnsw/agent/filter/traffic_action.h"
#include "oper/agent_types.h"
#include "vnsw/agent/cmn/agent_cmn.h"

class PacketHeader;
class AclEntryMatch {
public:
    virtual ~AclEntryMatch() {}
    virtual bool Match(const PacketHeader *packet_header) const = 0;
    virtual void SetAclEntryMatchSandeshData(AclEntrySandeshData &data) = 0;
};

struct Range {
    Range(const uint16_t minimum, const uint16_t maximum) :
    min(minimum), max(maximum) {}
    Range() : min(0), max(0) {}
    boost::intrusive::slist_member_hook<> node;
    uint16_t min;
    uint16_t max;
};

typedef boost::intrusive::member_hook<Range, 
                                      boost::intrusive::slist_member_hook<>, 
                                      &Range::node
                                     > RangeNode;
typedef boost::intrusive::slist<Range, 
                                RangeNode,
                                boost::intrusive::cache_last<true>
                               > RangeSList;

struct delete_disposer {
    void operator() (Range *range) { delete range;}
};


class PortMatch : public AclEntryMatch {
public:
    PortMatch() {}
    ~PortMatch() {port_ranges_.clear_and_dispose(delete_disposer());}
    void SetPortRange(const uint16_t min_port, const uint16_t max_port);
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data) = 0;
    virtual bool Match(const PacketHeader *packet_header) const = 0;
protected:
    RangeSList port_ranges_;
};

class SrcPortMatch : public PortMatch {
public:
    bool Match(const PacketHeader *packet_header) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
};
class DstPortMatch : public PortMatch {
public:
    bool Match(const PacketHeader *packet_header) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
};

class ProtocolMatch : public AclEntryMatch {
public:
    ProtocolMatch() {}
    ~ProtocolMatch() {protocol_ranges_.clear_and_dispose(delete_disposer());}
    void SetProtocolRange(const uint16_t min, const uint16_t max);
    bool Match(const PacketHeader *packet_header) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
private:
    RangeSList protocol_ranges_;
};

class AddressMatch : public AclEntryMatch {
public:
    typedef boost::uuids::uuid uuid;
    static const int kAny = -1;
    //typedef std::vector<uint32_t> sgl;
    enum AddressType {
       IP_ADDR = 1,
       NETWORK_ID = 2,
       SG = 3,
       UNKNOWN_TYPE = 4,
    };

    AddressMatch() {}
    ~AddressMatch() {}

    // Set source
    void SetSource(bool src);

    // Network policy address could by SG uuid or VN policy uuid
    void SetNetworkID(const uuid id);
    void SetNetworkIDStr(const std::string id);
    void SetSGId(const uint32_t id);
    // Set IP Address and mask
    void SetIPAddress(const IpAddress &ip, const IpAddress &mask);
    // Match packet header for address
    bool Match(const PacketHeader *packet_header) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
private:
    AddressType addr_type_;
    bool src_;

    // IP Address and mask
    IpAddress ip_addr_;
    IpAddress ip_mask_;
    int prefix_len;
    // Network policy or Security Group identifier
    uuid policy_id_;
    std::string policy_id_s_;
    int sg_id_;

    bool SGMatch(const SecurityGroupList &sg_l, int id) const;
    bool SGMatch(const SecurityGroupList *sg_l, int id) const;
};
#endif
