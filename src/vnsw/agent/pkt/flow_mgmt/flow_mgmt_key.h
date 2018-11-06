/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_PKT_FLOW_MGMT_KEY_H__
#define __AGENT_PKT_FLOW_MGMT_KEY_H__

#include <db/db_entry.h>
#include <pkt/flow_event.h>

class FlowMgmtKey {
public:
    enum Type {
        INVALID,
        INTERFACE,
        ACL,
        ACE_ID,
        VN,
        VM,
        INET4,
        INET6,
        BRIDGE,
        NH,
        VRF,
        BGPASASERVICE,
        END
    };

    FlowMgmtKey(Type type, const DBEntry *db_entry) :
        type_(type), db_entry_(db_entry) {
    }

    virtual ~FlowMgmtKey() { }

    // Clone the key
    virtual FlowMgmtKey *Clone() = 0;

    // Convert from FlowMgmtKey to FlowEvent
    virtual void KeyToFlowRequest(FlowEvent *req) {
        req->set_db_entry(db_entry_);
    }

    // Is DBEntry used as key. Routes dont use DBEntry as key
    virtual bool UseDBEntry() const { return true; }

    // Comparator
    virtual bool Compare(const FlowMgmtKey *rhs) const {
        return false;
    }

    bool IsLess(const FlowMgmtKey *rhs) const {
        if (type_ != rhs->type_)
            return type_ < rhs->type_;

        if (UseDBEntry()) {
            if (db_entry_ != rhs->db_entry_)
                return db_entry_ < rhs->db_entry_;
        }

        return Compare(rhs);
    }

    FlowEvent::Event FreeDBEntryEvent() const;
    Type type() const { return type_; }
    const DBEntry *db_entry() const { return db_entry_; }
    void set_db_entry(const DBEntry *db_entry) { db_entry_ = db_entry; }

protected:
    Type type_;
    mutable const DBEntry *db_entry_;

private:
    DISALLOW_COPY_AND_ASSIGN(FlowMgmtKey);
};

struct FlowMgmtKeyCmp {
    bool operator()(const FlowMgmtKey *l, const FlowMgmtKey *r) const {
        return l->IsLess(r);
    }
};

class AclFlowMgmtKey : public FlowMgmtKey {
public:
    AclFlowMgmtKey(const AclDBEntry *acl, const AclEntryIDList *ace_id_list) :
        FlowMgmtKey(FlowMgmtKey::ACL, acl) {
        if (ace_id_list) {
            ace_id_list_ = *ace_id_list;
        }
    }

    virtual ~AclFlowMgmtKey() { }

    virtual FlowMgmtKey *Clone() {
        return new AclFlowMgmtKey(static_cast<const AclDBEntry *>(db_entry()),
                                  &ace_id_list_);
    }

    const AclEntryIDList *ace_id_list() const { return &ace_id_list_; }

    void set_ace_id_list(const AclEntryIDList *list) {
        ace_id_list_ = *list;
    }

private:
    AclEntryIDList ace_id_list_;
    DISALLOW_COPY_AND_ASSIGN(AclFlowMgmtKey);
};

class VnFlowMgmtKey : public FlowMgmtKey {
public:
    VnFlowMgmtKey(const VnEntry *vn) : FlowMgmtKey(FlowMgmtKey::VN, vn) { }
    virtual ~VnFlowMgmtKey() { }

    virtual FlowMgmtKey *Clone() {
        return new VnFlowMgmtKey(static_cast<const VnEntry *>(db_entry()));
    }

private:
    DISALLOW_COPY_AND_ASSIGN(VnFlowMgmtKey);
};

class InterfaceFlowMgmtKey : public FlowMgmtKey {
public:
    InterfaceFlowMgmtKey(const Interface *intf) :
        FlowMgmtKey(FlowMgmtKey::INTERFACE, intf) {
    }

    virtual ~InterfaceFlowMgmtKey() { }

    virtual FlowMgmtKey *Clone() {
        return new InterfaceFlowMgmtKey(static_cast<const Interface *>(db_entry()));
    }

private:
    DISALLOW_COPY_AND_ASSIGN(InterfaceFlowMgmtKey);
};

class NhFlowMgmtKey : public FlowMgmtKey {
public:
    NhFlowMgmtKey(const NextHop *nh) : FlowMgmtKey(FlowMgmtKey::NH, nh) { }
    virtual ~NhFlowMgmtKey() { }

    virtual FlowMgmtKey *Clone() {
        return new NhFlowMgmtKey(static_cast<const NextHop *>(db_entry()));
    }

private:
    DISALLOW_COPY_AND_ASSIGN(NhFlowMgmtKey);
};

class RouteFlowMgmtKey : public FlowMgmtKey {
public:
    static Type AddrToType(const IpAddress &addr) {
        if (addr.is_v4())
            return INET4;

        if (addr.is_v6())
            return INET6;

        assert(0);
        return INVALID;
    }

    static Type RouteToType(const AgentRoute *rt) {
        const InetUnicastRouteEntry *inet_rt =
            dynamic_cast<const InetUnicastRouteEntry *>(rt);
        if (inet_rt) {
            return AddrToType(inet_rt->addr());
        }

        if (dynamic_cast<const BridgeRouteEntry *>(rt))
            return BRIDGE;

        assert(0);
    }

    RouteFlowMgmtKey(Type type, uint32_t vrf_id) :
        FlowMgmtKey(type, NULL), vrf_id_(vrf_id) {
    }

    RouteFlowMgmtKey(Type type, const AgentRoute *rt, uint32_t vrf_id) :
        FlowMgmtKey(type, rt), vrf_id_(vrf_id) {
    }

    virtual ~RouteFlowMgmtKey() { }

    virtual bool UseDBEntry() const { return false; }
    uint32_t vrf_id() const { return vrf_id_; }

protected:
    uint32_t vrf_id_;

private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtKey);
};

class InetRouteFlowMgmtKey : public RouteFlowMgmtKey {
public:
    InetRouteFlowMgmtKey(const InetUnicastRouteEntry *route) :
        RouteFlowMgmtKey(AddrToType(route->addr()), route, route->vrf_id()),
        ip_(route->addr()), plen_(route->plen()) {
    }

    InetRouteFlowMgmtKey(uint32_t vrf_id, const IpAddress &ip, uint8_t plen) :
        RouteFlowMgmtKey(AddrToType(ip), vrf_id), ip_(ip), plen_(plen) {
    }

    virtual ~InetRouteFlowMgmtKey() { }

    virtual bool Compare(const FlowMgmtKey *rhs) const {
        const InetRouteFlowMgmtKey *rhs_key =
            static_cast<const InetRouteFlowMgmtKey *>(rhs);
        if (vrf_id_ != rhs_key->vrf_id_)
            return vrf_id_ < rhs_key->vrf_id_;

        if (ip_ != rhs_key->ip_)
            return ip_ < rhs_key->ip_;

        return plen_ < rhs_key->plen_;
    }

    class KeyCmp {
    public:
        static std::size_t BitLength(const InetRouteFlowMgmtKey *rt) {
            return (((sizeof(rt->vrf_id_) + sizeof(rt->type_)) << 3) +
                    rt->plen_);
        }

        static char ByteValue(const InetRouteFlowMgmtKey *rt, std::size_t idx) {
            const char *ch;
            std::size_t i = idx;
            if (i < sizeof(rt->vrf_id_)) {
                ch = (const char *)&rt->vrf_id_;
                return ch[sizeof(rt->vrf_id_) - i - 1];
            }
            i -= sizeof(rt->vrf_id_);
            if (i < sizeof(rt->type_)) {
                ch = (const char *)&rt->type_;
                return ch[sizeof(rt->type_) - i - 1];
            }
            i -= sizeof(rt->type_);
            if (rt->type_ == INET4) {
                return rt->ip_.to_v4().to_bytes()[i];
            } else if (rt->type_ == INET6) {
                return rt->ip_.to_v6().to_bytes()[i];
            } else {
                assert(0);
                return 0;
            }
        }
    };

    FlowMgmtKey *Clone() {
        if (ip_.is_v4()) {
            return new InetRouteFlowMgmtKey(vrf_id_, ip_.to_v4(), plen_);
        }

        if (ip_.is_v6()) {
            return new InetRouteFlowMgmtKey(vrf_id_, ip_.to_v6(), plen_);
        }

        return NULL;
    }

    bool Match(const IpAddress &match_ip) const {
        if (ip_.is_v4()) {
            return (Address::GetIp4SubnetAddress(ip_.to_v4(), plen_) ==
                    Address::GetIp4SubnetAddress(match_ip.to_v4(), plen_));
        } else if (ip_.is_v6()) {
            return (Address::GetIp6SubnetAddress(ip_.to_v6(), plen_) ==
                    Address::GetIp6SubnetAddress(match_ip.to_v6(), plen_));
        }

        assert(0);
        return false;
    }

    bool NeedsReCompute(const FlowEntry *flow);
    IpAddress ip() const { return ip_; }
    uint8_t plen() const { return plen_; }

private:
    friend class InetRouteFlowMgmtTree;

    IpAddress ip_;
    uint8_t plen_;
    Patricia::Node node_;
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtKey);
};

class BridgeRouteFlowMgmtKey : public RouteFlowMgmtKey {
public:
    BridgeRouteFlowMgmtKey(const BridgeRouteEntry *rt) :
        RouteFlowMgmtKey(BRIDGE, rt, rt->vrf_id()), mac_(rt->mac()) {
    }

    BridgeRouteFlowMgmtKey(uint32_t vrf_id, const MacAddress &mac) :
        RouteFlowMgmtKey(BRIDGE, vrf_id), mac_(mac) {
    }

    virtual ~BridgeRouteFlowMgmtKey() { }

    virtual bool Compare(const FlowMgmtKey *rhs) const {
        const BridgeRouteFlowMgmtKey *rhs_key =
            static_cast<const BridgeRouteFlowMgmtKey *>(rhs);
        if (vrf_id_ != rhs_key->vrf_id_)
            return vrf_id_ < rhs_key->vrf_id_;

        return mac_ < rhs_key->mac_;
    }

    FlowMgmtKey *Clone() {
        return new BridgeRouteFlowMgmtKey(vrf_id(), mac_);
    }

private:
    friend class BridgeRouteFlowMgmtTree;

    MacAddress mac_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtKey);
};

class VrfFlowMgmtKey : public FlowMgmtKey {
public:
    VrfFlowMgmtKey(const VrfEntry *vrf) :
        FlowMgmtKey(FlowMgmtKey::VRF, vrf) {
    }

    virtual ~VrfFlowMgmtKey() { }

    virtual FlowMgmtKey *Clone() {
        return new VrfFlowMgmtKey(static_cast<const VrfEntry *>(db_entry()));
    }

private:
    DISALLOW_COPY_AND_ASSIGN(VrfFlowMgmtKey);
};

class BgpAsAServiceFlowMgmtKey : public FlowMgmtKey {
public:
    BgpAsAServiceFlowMgmtKey(const boost::uuids::uuid &uuid,
                             uint32_t source_port,
                             uint8_t cn_index,
                             HealthCheckInstanceBase *hc_instance,
                             HealthCheckService *hc_service) :
        FlowMgmtKey(FlowMgmtKey::BGPASASERVICE, NULL), uuid_(uuid),
        source_port_(source_port), cn_index_(cn_index),
        bgp_health_check_instance_(hc_instance),
        bgp_health_check_service_(hc_service) { }

    virtual ~BgpAsAServiceFlowMgmtKey() { }

    virtual FlowMgmtKey *Clone() {
        return new BgpAsAServiceFlowMgmtKey(uuid_, source_port_, cn_index_,
                                            bgp_health_check_instance_,
                                            bgp_health_check_service_);
    }

    virtual bool UseDBEntry() const { return false; }

    virtual bool Compare(const FlowMgmtKey *rhs) const {
        const BgpAsAServiceFlowMgmtKey *rhs_key =
            static_cast<const BgpAsAServiceFlowMgmtKey *>(rhs);
        if (uuid_ != rhs_key->uuid_)
            return uuid_ < rhs_key->uuid_;
        if (cn_index_ != rhs_key->cn_index_)
            return cn_index_< rhs_key->cn_index_;
        return source_port_ < rhs_key->source_port_;
    }

    const boost::uuids::uuid &uuid() const { return uuid_; }
    uint32_t source_port() const { return source_port_; }
    uint8_t cn_index() const { return cn_index_; }

    HealthCheckInstanceBase *bgp_health_check_instance() const {
        return bgp_health_check_instance_;
    }

    void StartHealthCheck(Agent *agent, FlowEntry *flow,
                          const boost::uuids::uuid &hc_uuid);
    void StopHealthCheck(FlowEntry *flow);

private:
    boost::uuids::uuid uuid_;
    uint32_t source_port_;
    uint8_t cn_index_; //Control node index
    // By adding the health check instance here, we ensure one BFD session
    // per <VMI-SRC-IP, BGP-DEST-IP, VMI>
    mutable HealthCheckInstanceBase *bgp_health_check_instance_;
    mutable HealthCheckService *bgp_health_check_service_;
    DISALLOW_COPY_AND_ASSIGN(BgpAsAServiceFlowMgmtKey);
};

#endif // __AGENT_PKT_FLOW_MGMT_KEY_H__
