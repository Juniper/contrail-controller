/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_ACL_ENTRY_MATCH_H__
#define __AGENT_ACL_ENTRY_MATCH_H__

#include <boost/ptr_container/ptr_list.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/uuid/uuid.hpp>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>

struct PacketHeader;
struct FlowPolicyInfo;

struct AclAddressInfo {
    IpAddress ip_addr;
    IpAddress ip_mask;
    int ip_plen;

    AclAddressInfo() : ip_addr(), ip_mask(), ip_plen(0) {
    }
    bool operator==(const AclAddressInfo& rhs) const {
        if (ip_addr == rhs.ip_addr && ip_mask == rhs.ip_mask) {
            return true;
        }
        return false;
    }
};

class AclEntryMatch {
public:
    enum Type {
        SOURCE_PORT_MATCH,
        DESTINATION_PORT_MATCH,
        PROTOCOL_MATCH,
        ADDRESS_MATCH,
        SERVICE_GROUP_MATCH,
        TAGS_MATCH
    };
    AclEntryMatch(Type type):type_(type) { }
    virtual ~AclEntryMatch() {}
    virtual bool Match(const PacketHeader *packet_header,
                       FlowPolicyInfo *info) const = 0;
    virtual void SetAclEntryMatchSandeshData(AclEntrySandeshData &data) = 0;
    virtual bool Compare(const AclEntryMatch &rhs) const = 0;
    bool operator ==(const AclEntryMatch &rhs) const {
        if (type_ != rhs.type_) {
            return false;
        }
        return Compare(rhs);
    }
private:
    Type type_;
};

struct Range {
    Range(const uint16_t minimum, const uint16_t maximum) :
    min(minimum), max(maximum) {}
    Range() : min(0), max(0) {}
    boost::intrusive::slist_member_hook<> node;
    uint16_t min;
    uint16_t max;
    bool operator==(const Range &rhs) const {
        if (min == rhs.min && max == rhs.max) {
            return true;
        }
        return false;
    }
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
    PortMatch(Type type): AclEntryMatch(type) {}
    ~PortMatch() {port_ranges_.clear_and_dispose(delete_disposer());}
    void SetPortRange(const uint16_t min_port, const uint16_t max_port);
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data) = 0;
    virtual bool Match(const PacketHeader *packet_header,
                       FlowPolicyInfo *info) const = 0;
    virtual bool Compare(const AclEntryMatch &rhs) const;
    bool CheckPortRanges(const uint16_t min_port,
                       const uint16_t max_port) const;
protected:
    RangeSList port_ranges_;
};

class SrcPortMatch : public PortMatch {
public:
    SrcPortMatch(): PortMatch(SOURCE_PORT_MATCH) {}
    bool Match(const PacketHeader *packet_header,
               FlowPolicyInfo *info) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
};
class DstPortMatch : public PortMatch {
public:
    DstPortMatch(): PortMatch(DESTINATION_PORT_MATCH) {}
    bool Match(const PacketHeader *packet_header,
               FlowPolicyInfo *info) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
};

class ProtocolMatch : public AclEntryMatch {
public:
    ProtocolMatch(): AclEntryMatch(PROTOCOL_MATCH) {}
    ~ProtocolMatch() {protocol_ranges_.clear_and_dispose(delete_disposer());}
    void SetProtocolRange(const uint16_t min, const uint16_t max);
    bool Match(const PacketHeader *packet_header,
               FlowPolicyInfo *info) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
    virtual bool Compare(const AclEntryMatch &rhs) const;

private:
    RangeSList protocol_ranges_;
};

class ServicePort {
public:
    Range protocol;
    Range src_port;
    Range dst_port;

    bool operator==(const ServicePort &rhs) const {
        if (protocol == rhs.protocol &&
            src_port == rhs.src_port&&
            dst_port == rhs.dst_port) {
            return true;
        }
        return false;
    }

    bool PortMatch(uint32_t sport, uint32_t dport) const;
};

class ServiceGroupMatch : public AclEntryMatch {
public:
    typedef std::vector<ServicePort> ServicePortList;

    ServiceGroupMatch(ServicePortList service_port_list):
        AclEntryMatch(SERVICE_GROUP_MATCH),
        service_port_list_(service_port_list) {}
    ~ServiceGroupMatch() {};

    bool Match(const PacketHeader *packet_header,
               FlowPolicyInfo *info) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
    virtual bool Compare(const AclEntryMatch &rhs) const;

    size_t size() const {
        return service_port_list_.size();
    }

private:
    ServicePortList service_port_list_;
};


class TagsMatch : public AclEntryMatch {
public:
    TagsMatch(TagList tag_list) :
        AclEntryMatch(TAGS_MATCH),
        tag_list_(tag_list) {}
    ~TagsMatch() {};

    bool Match(const PacketHeader *packet_header,
               FlowPolicyInfo *info) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
    virtual bool Compare(const AclEntryMatch &rhs) const;

    size_t size() const {
        return tag_list_.size();
    }

    const TagList& tag_list() const {
        return tag_list_;
    }

private:
    TagList tag_list_;
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
       TAGS = 4,
       ADDRESS_GROUP = 5,
       UNKNOWN_TYPE = 6,
    };

    AddressMatch():AclEntryMatch(ADDRESS_MATCH) {}
    ~AddressMatch() {}

    // Set source
    void SetSource(bool src);

    // Network policy address could by SG uuid or VN policy uuid
    void SetNetworkID(const uuid id);
    void SetNetworkIDStr(const std::string id);
    void SetSGId(const uint32_t id);
    void SetTags(const TagList &tags) {
        addr_type_ = TAGS;
        tags_ = tags;
    }

    void SetAddressGroup(const std::vector<AclAddressInfo> &list,
                         const TagList &tags);

    const TagList& tags() const {
        return tags_;
    }
    // Set IP Address and mask
    void SetIPAddress(const std::vector<AclAddressInfo> &list);
    // Match packet header for address
    bool Match(const PacketHeader *packet_header,
               FlowPolicyInfo *info) const;
    void SetAclEntryMatchSandeshData(AclEntrySandeshData &data);
    virtual bool Compare(const AclEntryMatch &rhs) const;
    static std::string BuildIpMaskList(const std::vector<AclAddressInfo> &list);
    static std::string BuildTags(const TagList &list);
    size_t ip_list_size() const {
        return ip_list_.size();
    }
private:
    AddressType addr_type_;
    bool src_;

    // IP Address and mask
    std::vector<AclAddressInfo> ip_list_;
    // Network policy or Security Group identifier
    uuid policy_id_;
    std::string policy_id_s_;
    int sg_id_;
    TagList tags_;

    bool SGMatch(const SecurityGroupList &sg_l, int id) const;
    bool SGMatch(const SecurityGroupList *sg_l, int id) const;
    bool TagsMatch(const TagList &tags) const;
    bool TagsMatchAG(const TagList &tags) const;
    bool AddressGroupMatch(const IpAddress &ip, const TagList &tags) const;
};
#endif
