/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <cfg/init_config.h>
#include <route/route.h>

#include <oper/vrf.h>
#include <oper/inet4_route.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

SandeshTraceBufferPtr AgentDBwalkTraceBuf(SandeshTraceBufferCreate(
    AGENT_DBWALK_TRACE_BUF, 1000));

/////////////////////////////////////////////////////////////////////////////
// Route Table methods
/////////////////////////////////////////////////////////////////////////////
class Inet4RouteTable::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(Inet4RouteTable *rt_table) : 
        LifetimeActor(Agent::GetLifetimeManager()), table_(rt_table) { 
    }
    virtual ~DeleteActor() { 
    }
    virtual bool MayDelete() const {
        if (table_->HasListeners() || table_->Size() != 0) {
            return false;
        }
        return true;
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        //Release refernce to VRF
        table_->vrf_delete_ref_.Reset(NULL);
        table_->SetVrfEntry(NULL);
    }

  private:
    Inet4RouteTable *table_;
};

Inet4RouteTable::Inet4RouteTable(DB *db, const std::string &name) : 
    RouteTable(db, name), db_(db), deleter_(new DeleteActor(this)), 
    vrf_delete_ref_(this, NULL) { 
}

void Inet4RouteTable::SetVrfEntry(VrfEntryRef vrf) {
    vrf_entry_ = vrf;
}

void Inet4RouteTable::SetVrfDeleteRef(LifetimeActor *ref) {
    vrf_delete_ref_.Reset(ref);
}

LifetimeActor *Inet4RouteTable::deleter() {
    return deleter_.get();
}

void Inet4RouteTable::ManagedDelete() {
    //Delete all the routes
    DeleteAllRoutes();
    deleter_->Delete();
}

void Inet4RouteTable::MayResumeDelete(bool is_empty) {
    if (!deleter()->IsDeleted()) {
        return;
    }

    //
    // If the table has entries, deletion cannot be resumed
    //
    if (!is_empty) {
        return;
    }

    Agent::GetLifetimeManager()->Enqueue(deleter());
}

Inet4Route *Inet4RouteTable::FindActiveEntry(const Inet4Route *key) {
    Inet4Route *entry = static_cast<Inet4Route *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

Inet4Route *Inet4RouteTable::FindActiveEntry(const Inet4RouteKey *key) {
    Inet4Route *entry = static_cast<Inet4Route *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

NextHop *Inet4RouteTable::FindNextHop(NextHopKey *key) const {
    return static_cast<NextHop *>(Agent::GetNextHopTable()->FindActiveEntry(key));
}

VrfEntry *Inet4RouteTable::FindVrfEntry(const string &vrf_name) const {
    return Agent::GetVrfTable()->FindVrfFromName(vrf_name);
}

void Inet4RouteTable::DeleteRouteDone(DBTableBase *base, 
                                      RouteTableWalkerState *state) {
    LOG(DEBUG, "Deleted all BGP injected routes for " << base->name());
    delete state;
}

bool Inet4RouteTable::DelWalkerCb(DBTablePartBase *part,
                                             DBEntryBase *entry) {
    return DelExplicitRoute(part, entry);
}

void Inet4RouteTable::DeleteAllRoutes() {
    DBTableWalker *walker = Agent::GetDB()->GetWalker();
    RouteTableWalkerState *state = new RouteTableWalkerState(deleter());
    walker->WalkTable(this, NULL, 
            boost::bind(&Inet4RouteTable::DelWalkerCb, this, _1, _2),
            boost::bind(&Inet4RouteTable::DeleteRouteDone, this, _1, state));
}

/////////////////////////////////////////////////////////////////////////////
// Route Entry methods
/////////////////////////////////////////////////////////////////////////////
Inet4Route::Inet4Route(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen, RtType type) : 
        Route(), vrf_(vrf), addr_(addr), plen_(plen) {
    uint32_t mask = plen ? (0xFFFFFFFF << (32 - plen)) : 0;
    addr_ = ip::address_v4(addr.to_ulong() & mask);
    rt_type_ = type;
};

Inet4Route::Inet4Route(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen) : 
        Route(), vrf_(vrf), addr_(addr), plen_(plen) {
    uint32_t mask = plen ? (0xFFFFFFFF << (32 - plen)) : 0;
    addr_ = ip::address_v4(addr.to_ulong() & mask);
};

Inet4Route::Inet4Route(VrfEntry *vrf, const Ip4Address &addr, RtType type) : 
        Route(), vrf_(vrf), addr_(addr) {
    addr_ = ip::address_v4(addr.to_ulong());
    plen_ = 32;
    rt_type_ = type;
};

Inet4RouteTable::~Inet4RouteTable() {
}

std::string Inet4Route::ToString() const {
    ostringstream str;
    str << addr_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

uint32_t Inet4Route::GetVrfId() const {
    return vrf_->GetVrfId();
}

void Inet4Route::FillTrace(RouteInfo &rt_info, Trace event, 
                           const AgentPath *path) {
    rt_info.set_ip(addr_.to_string());
    rt_info.set_vrf(GetVrfEntry()->GetName());

    switch(event) {
    case ADD:{
        rt_info.set_op("ADD");
        break;
    }

    case DELETE: {
        rt_info.set_op("DELETE");
        break;
    }

    case ADD_PATH:
    case DELETE_PATH:
    case CHANGE_PATH: {
        if (path->GetPeer()) {
            rt_info.set_peer(path->GetPeer()->GetName());
        }
        const NextHop *nh = path->GetNextHop();
        switch (nh->GetType()) {
        case NextHop::TUNNEL: {
            const TunnelNH *tun = static_cast<const TunnelNH *>(nh);
            rt_info.set_nh_type("TUNNEL");
            rt_info.set_dest_server(tun->GetDip()->to_string());
            rt_info.set_dest_server_vrf(tun->GetVrf()->GetName());
            break;
        }

        case NextHop::ARP:{
            rt_info.set_nh_type("DIRECT");
            break;
        }

        case NextHop::INTERFACE: {
            const InterfaceNH *intf_nh = static_cast<const InterfaceNH *>(nh);
            rt_info.set_nh_type("INTERFACE");
            rt_info.set_intf(intf_nh->GetInterface()->GetName());
            break;
        }

        case NextHop::RECEIVE: {
            const ReceiveNH *rcv_nh = static_cast<const ReceiveNH *>(nh);
            rt_info.set_nh_type("RECEIVE");
            rt_info.set_intf(rcv_nh->GetInterface()->GetName());
            break;
        }

        case NextHop::DISCARD: {
            rt_info.set_nh_type("DISCARD");
            break;
        }

        case NextHop::VLAN: {
            rt_info.set_nh_type("VLAN");
            break;
        }

        case NextHop::RESOLVE: {
            rt_info.set_nh_type("RESOLVE");
            break;
        }

        case NextHop::COMPOSITE: {
            rt_info.set_nh_type("COMPOSITE");
            break;
        }
  
        default:
            assert(0);
            break;
        }
        
        if (event == ADD_PATH) {
            rt_info.set_op("PATH ADD");
        } else if (event == CHANGE_PATH) {
            rt_info.set_op("PATH CHANGE");
        } else if (event == DELETE_PATH) {
            rt_info.set_op("PATH DELETE");
        }
        break;
    }
    }
}

void Inet4Route::SetVrf(VrfEntryRef vrf) {
    vrf_ = vrf;
}

void Inet4Route::SetAddr(Ip4Address addr) {
    addr_ = addr;
}

void Inet4Route::SetPlen(uint8_t plen) {
    plen_ = plen;
}


Ip4Address Inet4Route::GetSrcIpAddress() const {
     Ip4Address addr(0);
     return addr;
}
