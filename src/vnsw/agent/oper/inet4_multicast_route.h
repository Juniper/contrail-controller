/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_inet4_multicast_route_hpp
#define vnsw_inet4_multicast_route_hpp

//////////////////////////////////////////////////////////////////
//  MULTICAST INET4
/////////////////////////////////////////////////////////////////
class Inet4MulticastAgentRouteTable : public AgentRouteTable {
public:
    Inet4MulticastAgentRouteTable(DB *db, const std::string &name) :
        AgentRouteTable(db, name),
        walkid_(DBTableWalker::kInvalidWalkerId) { };
    virtual ~Inet4MulticastAgentRouteTable() { };
    // Nexthop will be stored in path as localvmpeer so that it falls in line
    // Override virtual routines for no action w.r.t. multicast
    virtual string GetTableName() const {
        return "Inet4MulticastAgentRouteTable";
    }
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::INET4_MULTICAST;
    }

    void McRtRouteNotifyDone(DBTableBase *base, DBState *);
    void AddVHostRecvRoute(const string &vm_vrf,
                           const string &interface_name,
                           const Ip4Address &addr,
                           bool policy);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void AddMulticastRoute(const string &vn_name, const string &vrf_name,
                                  const Ip4Address &src_addr,
                                  const Ip4Address &grp_addr,
                                  ComponentNHKeyList &component_nh_key_list);
    static void DeleteMulticastRoute(const string &vrf_name, 
                                     const Ip4Address &src_addr,
                                     const Ip4Address &grp_addr); 
    static void Delete(const string &vrf_name, const Ip4Address &src_addr,
                       const Ip4Address &grp_addr);
private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Inet4MulticastAgentRouteTable);
};

class Inet4MulticastRouteEntry : public AgentRoute {
public:
    Inet4MulticastRouteEntry(VrfEntry *vrf, const Ip4Address &dst, 
                             const Ip4Address &src) :
        AgentRoute(vrf, true), dst_addr_(dst), src_addr_(src) {
    }
    virtual ~Inet4MulticastRouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual const string GetAddressString() const {
        return dst_addr_.to_string();
    }
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::INET4_MULTICAST;
    }

    void set_dest_ip_addr(const Ip4Address &dst) {dst_addr_ = dst;}
    void set_src_ip_addr(const Ip4Address &src) {src_addr_ = src;}
    const Ip4Address &src_ip_addr() const {return src_addr_;}
    const Ip4Address &dest_ip_addr() const {return dst_addr_;}

private:
    Ip4Address dst_addr_;
    Ip4Address src_addr_;
    DISALLOW_COPY_AND_ASSIGN(Inet4MulticastRouteEntry);
};

class Inet4MulticastRouteKey : public AgentRouteKey {
public:
    Inet4MulticastRouteKey(const string &vrf_name,const Ip4Address &dip, 
                           const Ip4Address &sip) :
        AgentRouteKey(Agent::GetInstance()->multicast_peer(), vrf_name),
        dip_(dip), sip_(sip) {
    }
    Inet4MulticastRouteKey(const string &vrf_name, const Ip4Address &dip) :
        AgentRouteKey(Agent::GetInstance()->multicast_peer(), vrf_name),
        dip_(dip) { 
        boost::system::error_code ec;
        sip_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
    }
    Inet4MulticastRouteKey(const string &vrf_name) : 
        AgentRouteKey(Agent::GetInstance()->multicast_peer(), vrf_name) {
            boost::system::error_code ec;
            dip_ =  IpAddress::from_string("255.255.255.255", ec).to_v4();
            sip_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
    }
    virtual ~Inet4MulticastRouteKey() { }
    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    virtual Agent::RouteTableType GetRouteTableType() {
       return Agent::INET4_MULTICAST;
    }
    virtual AgentRouteKey *Clone() const;
    virtual string ToString() const;

    const Ip4Address &dest_ip_addr() const {return dip_;}
    const Ip4Address &src_ip_addr() const {return sip_;}

private:
    Ip4Address dip_;
    Ip4Address sip_;
    DISALLOW_COPY_AND_ASSIGN(Inet4MulticastRouteKey);
};

#endif // vnsw_inet4_multicast_route_hpp
