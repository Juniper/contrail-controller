#ifndef __AGENT_FLOW_TABLE_MGMT_ROUTE_H__
#define __AGENT_FLOW_TABLE_MGMT_ROUTE_H__

#include "flow_mgmt.h"

class VrfFlowMgmtTree;

class RouteFlowMgmtKey : public FlowMgmtKey {
public:
    static Type AddrToType(const IpAddress &addr) {
        if (addr.is_v4())
            return INET4;

        if (addr.is_v6())
            return INET6;

        assert(0);
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

class RouteFlowMgmtEntry : public FlowMgmtEntry {
public:
    RouteFlowMgmtEntry() : FlowMgmtEntry() { }
    ~RouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtEntry);
};

class RouteFlowMgmtTree : public FlowMgmtTree {
public:
    RouteFlowMgmtTree(FlowMgmtManager *mgr) : FlowMgmtTree(mgr) { }
    virtual ~RouteFlowMgmtTree() { }
    virtual bool HasVrfFlows(uint32_t vrf_id) = 0;

    virtual bool Delete(FlowMgmtKey *key, FlowEntry *flow);
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);
    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);
private:
    DISALLOW_COPY_AND_ASSIGN(RouteFlowMgmtTree);
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

private:
    friend class InetRouteFlowMgmtTree;
    IpAddress ip_;
    uint8_t plen_;
    Patricia::Node node_;
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtKey);
};

class InetRouteFlowMgmtEntry : public RouteFlowMgmtEntry {
public:
    InetRouteFlowMgmtEntry() : RouteFlowMgmtEntry() { }
    ~InetRouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtEntry);
};

class InetRouteFlowMgmtTree : public RouteFlowMgmtTree {
public:
     typedef Patricia::Tree<InetRouteFlowMgmtKey, &InetRouteFlowMgmtKey::node_,
             InetRouteFlowMgmtKey::KeyCmp> LpmTree;

    InetRouteFlowMgmtTree(FlowMgmtManager *mgr) : RouteFlowMgmtTree(mgr) { }
    virtual ~InetRouteFlowMgmtTree() { }

    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree, uint32_t vrf,
                     const IpAddress &ip, uint8_t plen);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                     const IpAddress &ip, const FlowRouteRefMap *rt_list);
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);

    virtual bool OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key);
    virtual bool OperEntryDelete(const FlowMgmtRequest *req, FlowMgmtKey *key);
    FlowMgmtEntry *Allocate();
    bool HasVrfFlows(uint32_t vrf_id);

    InetRouteFlowMgmtKey *LPM(const InetRouteFlowMgmtKey *key) {
        if (key->plen_ == 0)
            return NULL;
        return lpm_tree_.LPMFind(key);
    }

    void AddToLPMTree(InetRouteFlowMgmtKey *key) {
        InetRouteFlowMgmtKey *rt_key =
            static_cast<InetRouteFlowMgmtKey *>(key->Clone());
        if (lpm_tree_.Insert(rt_key) == false)
            delete rt_key;
    }

    void DelFromLPMTree(InetRouteFlowMgmtKey *key) {
        InetRouteFlowMgmtKey *rt_key = lpm_tree_.Find(key);
        if (rt_key != NULL) {
            lpm_tree_.Remove(rt_key);
            delete rt_key;
        }
    }
private:
    LpmTree lpm_tree_;
    DISALLOW_COPY_AND_ASSIGN(InetRouteFlowMgmtTree);
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

class BridgeRouteFlowMgmtEntry : public RouteFlowMgmtEntry {
public:
    BridgeRouteFlowMgmtEntry() : RouteFlowMgmtEntry() { }
    ~BridgeRouteFlowMgmtEntry() { }

private:
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtEntry);
};

class BridgeRouteFlowMgmtTree : public RouteFlowMgmtTree {
public:
    BridgeRouteFlowMgmtTree(FlowMgmtManager *mgr) : RouteFlowMgmtTree(mgr) { }
    virtual ~BridgeRouteFlowMgmtTree() { }
    void ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree);
    FlowMgmtEntry *Allocate();
    bool HasVrfFlows(uint32_t vrf_id);
private:
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteFlowMgmtTree);
};

////////////////////////////////////////////////////////////////////////////
// Flow Management tree for VRF. VRF tree does not follow the normal pattern
// for other DBEntries.
//
// VRF flow management implements following functions,
// 1. Generate event to delete VRF when all flows for a VRF are deleted
// 2. Implement lifetime reference to Route Table for INET/Bridge DBTables
//    The route-table must be present till all flows relavent for the flow are
//    present.
//    FlowLifetimeRef implements Lifetime actor on the route-table till all
//    flows for the route-table are deleted
//
// When a flow-add is got, we dont really check for presence of VRF or not.
// When adding a flow, FlowTable must ensure that VRF is valid at that time
//
// FlowTable module on the other hand, ensures that flow is not added on a
// non-existing or a deleted VRF
//
// The routes used in flow-entry refer to VRF by vrf-id (ex. RouteFlowRefMap).
// Hence, we dont have VRF pointer in all cases. Instead, we store vrf-id as
// key
////////////////////////////////////////////////////////////////////////////
class VrfFlowMgmtEntry {
public:
    struct Data {
        Data(VrfFlowMgmtEntry *vrf_mgmt_entry, const VrfEntry *vrf,
             AgentRouteTable *table);
        virtual ~Data();
        void ManagedDelete();
        bool deleted() const { return deleted_; }

        bool deleted_;
        LifetimeRef<Data> table_ref_;
        VrfFlowMgmtEntry *vrf_mgmt_entry_;
        const VrfEntry *vrf_;
    };

    VrfFlowMgmtEntry(VrfFlowMgmtTree *vrf_tree, const VrfEntry *vrf);
    virtual ~VrfFlowMgmtEntry() { }
    bool CanDelete() const;

    VrfFlowMgmtTree *vrf_tree() const { return vrf_tree_; }
    uint32_t vrf_id() const { return vrf_id_; }
private:
    friend class VrfFlowMgmtTree;
    const VrfEntry *vrf_;
    uint32_t vrf_id_;
    Data inet4_;
    Data inet6_;
    Data bridge_;
    // Back reference for the tree
    VrfFlowMgmtTree *vrf_tree_;
    DISALLOW_COPY_AND_ASSIGN(VrfFlowMgmtEntry);
};

class VrfFlowMgmtTree {
public:
    typedef std::map<uint32_t, VrfFlowMgmtEntry *> Tree;
    VrfFlowMgmtTree(FlowMgmtManager *mgr) : mgr_(mgr) { }
    virtual ~VrfFlowMgmtTree() { }

    void OperEntryAdd(const VrfEntry *vrf);
    void OperEntryDelete(const VrfEntry *vrf);
    void RetryDelete(uint32_t vrf_id);
    bool CanDelete(uint32_t vrf_id);
    FlowMgmtManager *mgr() const { return mgr_; }
private:
    FlowMgmtManager *mgr_;
    Tree tree_;
    DISALLOW_COPY_AND_ASSIGN(VrfFlowMgmtTree);
};

#endif //  __AGENT_FLOW_TABLE_MGMT_ROUTE_H__
