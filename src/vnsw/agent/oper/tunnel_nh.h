/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_tunnel_nh_hpp
#define vnsw_agent_tunnel_nh_hpp

#include <base/dependency.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <oper/nexthop.h>
#include <oper/agent_route.h>

class TunnelNH : public NextHop {
public:
    TunnelNH(VrfEntry *vrf, const Ip4Address &sip, const Ip4Address &dip,
             bool policy, TunnelType type);
    virtual ~TunnelNH();

    virtual std::string ToString() const { 
        return "Tunnel to " + dip_.to_string();
    }
    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    const uint32_t vrf_id() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};
    const Ip4Address *GetSip() const {return &sip_;};
    const Ip4Address *GetDip() const {return &dip_;};
    const AgentRoute *GetRt() const {return arp_rt_.get();};
    const MacAddress *GetDmac() const {return &dmac_;}
    const TunnelType &GetTunnelType() const {return tunnel_type_;};
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }

    virtual bool MatchEgressData(const NextHop *nh) const {
        const TunnelNH *tun_nh = dynamic_cast<const TunnelNH *>(nh);
        if (tun_nh && vrf_ == tun_nh->vrf_ && dip_ == tun_nh->dip_) {
            return true;
        }
        return false;
    }
    virtual bool NeedMplsLabel() { return false; }
private:
    VrfEntryRef vrf_;
    Ip4Address sip_;
    Ip4Address dip_;
    TunnelType tunnel_type_;
    DependencyRef<NextHop, AgentRoute> arp_rt_;
    InterfaceConstRef interface_;
    MacAddress dmac_;
    DISALLOW_COPY_AND_ASSIGN(TunnelNH);
};

/////////////////////////////////////////////////////////////////////////////
// Mirror NH definition
/////////////////////////////////////////////////////////////////////////////
class MirrorNHKey : public NextHopKey {
public:
    MirrorNHKey(const string &vrf_name, const IpAddress &sip,
                const uint16_t sport, const IpAddress &dip,
                const uint16_t dport) :
        NextHopKey(NextHop::MIRROR, false), vrf_key_(vrf_name), sip_(sip),
        dip_(dip), sport_(sport), dport_(dport){
    };
    virtual ~MirrorNHKey() { };

    virtual NextHop *AllocEntry() const;
    virtual NextHopKey *Clone() const {
        return new MirrorNHKey(vrf_key_.name_, sip_, sport_, dip_, dport_);
    }
private:
    friend class MirrorNH;
    VrfKey vrf_key_;
    IpAddress sip_;
    IpAddress dip_;
    uint16_t sport_;
    uint16_t dport_;
    DISALLOW_COPY_AND_ASSIGN(MirrorNHKey);
};

class MirrorNHData : public NextHopData {
public:
    MirrorNHData() : NextHopData() {};
    virtual ~MirrorNHData() { };
private:
    friend class MirrorNH;
    DISALLOW_COPY_AND_ASSIGN(MirrorNHData);
};

class MirrorNH : public NextHop {
public:
    MirrorNH(const VrfKey &vkey, const IpAddress &sip, uint16_t sport,
             const IpAddress &dip, uint16_t dport);
    virtual ~MirrorNH() { }

    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool ChangeEntry(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    const uint32_t vrf_id() const;
    const VrfEntry *GetVrf() const {return vrf_.get();}
    const IpAddress *GetSip() const {return &sip_;}
    const uint16_t GetSPort() const {return sport_;}
    const IpAddress *GetDip() const {return &dip_;}
    const uint16_t GetDPort() const {return dport_;}
    const AgentRoute *GetRt() const {return arp_rt_.get();}
    virtual std::string ToString() const {
        return "Mirror to " + dip_.to_string();
    }
    virtual void SendObjectLog(const NextHopTable *table,
                               AgentLogEvent::type event) const;
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
    virtual bool MatchEgressData(const NextHop *nh) const {
        const MirrorNH *mir_nh = dynamic_cast<const MirrorNH *>(nh);
        if (mir_nh && vrf_ == mir_nh->vrf_ && dip_ == mir_nh->dip_) {
            return true;
        }
        return false;
    }
    virtual bool NeedMplsLabel() { return false; }
private:
    InetUnicastAgentRouteTable *GetRouteTable();
    std::string vrf_name_;
    VrfEntryRef vrf_;
    IpAddress sip_;
    uint16_t sport_;
    IpAddress dip_;
    uint16_t dport_;
    DependencyRef<NextHop, AgentRoute> arp_rt_;
    InterfaceConstRef interface_;
    MacAddress dmac_;
    DISALLOW_COPY_AND_ASSIGN(MirrorNH);
};
#endif
