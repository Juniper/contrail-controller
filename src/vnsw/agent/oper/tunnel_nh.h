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
    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    const uint32_t vrf_id() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};
    const Ip4Address *GetSip() const {return &sip_;};
    const Ip4Address *GetDip() const {return &dip_;};
    const AgentRoute *GetRt() const {return arp_rt_.get();};
    const TunnelType &GetTunnelType() const {return tunnel_type_;};
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
private:
    VrfEntryRef vrf_;
    Ip4Address sip_;
    Ip4Address dip_;
    TunnelType tunnel_type_;
    DependencyRef<NextHop, AgentRoute> arp_rt_;
    DISALLOW_COPY_AND_ASSIGN(TunnelNH);
};

/////////////////////////////////////////////////////////////////////////////
// Mirror NH definition
/////////////////////////////////////////////////////////////////////////////
class MirrorNHKey : public NextHopKey {
public:
    MirrorNHKey(const string &vrf_name, const Ip4Address &sip,
                const uint16_t sport, const Ip4Address &dip,
                const uint16_t dport) :
        NextHopKey(NextHop::MIRROR, false), vrf_key_(vrf_name), sip_(sip),
        dip_(dip), sport_(sport), dport_(dport) { 
    };
    virtual ~MirrorNHKey() { };

    virtual NextHop *AllocEntry() const;
private:
    friend class MirrorNH;
    VrfKey vrf_key_;
    Ip4Address sip_;
    Ip4Address dip_;
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
    MirrorNH(VrfEntry *vrf, const Ip4Address &sip, uint16_t sport,
             const Ip4Address &dip, uint16_t dport):
        NextHop(NextHop::MIRROR, false, false), vrf_(vrf), sip_(sip),
        sport_(sport), dip_(dip), dport_(dport), arp_rt_(this) { };
    virtual ~MirrorNH() { };

    virtual bool NextHopIsLess(const DBEntry &rhs) const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool Change(const DBRequest *req);
    virtual void Delete(const DBRequest *req);
    virtual KeyPtr GetDBRequestKey() const;
    virtual bool CanAdd() const;

    const uint32_t vrf_id() const;
    const VrfEntry *GetVrf() const {return vrf_.get();};
    const Ip4Address *GetSip() const {return &sip_;};
    const uint16_t GetSPort() const {return sport_;}; 
    const Ip4Address *GetDip() const {return &dip_;};
    const uint16_t GetDPort() const {return dport_;};
    const AgentRoute *GetRt() const {return arp_rt_.get();};
    virtual std::string ToString() const { return "Mirror to " + dip_.to_string(); };
    virtual void SendObjectLog(AgentLogEvent::type event) const;
    virtual bool DeleteOnZeroRefCount() const {
        return true;
    }
private:
    VrfEntryRef vrf_;
    Ip4Address sip_;
    uint16_t sport_;
    Ip4Address dip_;
    uint16_t dport_;
    DependencyRef<NextHop, AgentRoute> arp_rt_;
    DISALLOW_COPY_AND_ASSIGN(MirrorNH);
};
#endif
