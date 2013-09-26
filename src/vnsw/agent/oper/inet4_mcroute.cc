/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <cfg/init_config.h>
#include <route/route.h>

#include <oper/vrf.h>
#include <oper/inet4_route.h>
#include <oper/inet4_mcroute.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

Inet4McRouteTable *Inet4McRouteTable::mc_route_table_;

Inet4McRouteTable::Inet4McRouteTable(DB *db, const std::string &name) :
    Inet4RouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {
}

Inet4McRouteTable::~Inet4McRouteTable() {
}


auto_ptr<DBEntry> Inet4McRouteTable::AllocEntry(const DBRequestKey *k) const {
    const Inet4McRouteKey *key = static_cast<const Inet4McRouteKey *>(k);
    VrfKey vrf_key(key->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable()->Find(&vrf_key, true));
    Inet4McRoute *route = new Inet4McRoute(vrf, key->src_, key->addr_);
    return auto_ptr<DBEntry>(static_cast<DBEntry *>(route));
}

DBTableBase *Inet4McRouteTable::CreateTable(DB *db, const std::string &name) {
    Inet4McRouteTable *table;
    table = new Inet4McRouteTable(db, name);
    table->Init();

    size_t index = name.rfind(VrfTable::GetInet4McSuffix());
    assert(index != string::npos);
    string vrf = name.substr(0, index);
    VrfEntry *vrf_entry = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf);
    assert(vrf_entry);
    table->SetVrfEntry(vrf_entry);
    table->SetVrfDeleteRef(vrf_entry->deleter());

    if (mc_route_table_ == NULL)
        mc_route_table_ = table;

    return table;
};

NextHop *Inet4McRouteTable::GetMcNextHop(Inet4McRouteKey *key, Inet4RouteData *data) {
    Inet4McastRoute *mcast_data = static_cast < Inet4McastRoute *> (data);
    NextHop *nh = NULL;

    switch (data->type_) {
      case Inet4RouteData::RECEIVE_ROUTE: {
          Inet4McReceiveRoute *vhost = static_cast<Inet4McReceiveRoute *>(data);
          ReceiveNHKey nhkey(vhost->intf_.Clone(), vhost->policy_);
          nh = static_cast<NextHop *>
              (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&nhkey));
          break;
      }
      default: {
          CompositeNHKey nhkey(key->vrf_name_, mcast_data->grp_addr_,
                                mcast_data->src_addr_, false);
          nh = static_cast<NextHop *>
              (Agent::GetInstance()->GetNextHopTable()->FindActiveEntry(&nhkey));
          assert(nh);
          /*
          CompositeNH *cnh = static_cast<CompositeNH *>(nh);
          if (cnh->IsMarkedForDeletion() == true) {
              nhkey.DiscardInit();
              nh = static_cast<NextHop *>(Agent::GetInstance()->GetNextHopTable()->Find(&nhkey));
          }
          */
          break;
       }
    }

    return nh;
}

void Inet4McRouteTable::Input(DBTablePartition *part, DBClient *client,
                            DBRequest *req) {
    Inet4McRouteKey *key = static_cast<Inet4McRouteKey *>(req->key.get());
    Inet4RouteData *data = static_cast<Inet4RouteData *>(req->data.get());
    Inet4McRouteTable *table = static_cast<Inet4McRouteTable *>(part->parent());
    Inet4McRoute *rt = NULL;

    VrfEntry *vrf = table->FindVrfEntry(key->vrf_name_);
    if (!vrf && req->oper == DBRequest::DB_ENTRY_DELETE) {
        return;
    } else {
        if (!vrf) {
            LOG(DEBUG, "Mc Route ignrored. VRF <" << key->vrf_name_ 
                << "> not found.");
            return;
        }
    }

    Inet4McRouteTable *vrf_table = vrf->GetInet4McRouteTable();
    if (vrf_table != this) {
        DBTablePartition *p = static_cast<DBTablePartition *>
            (vrf_table->GetTablePartition(key));
        vrf_table->Input(p, client, req);
        return;
    }

    rt = static_cast<Inet4McRoute *>(part->Find(key));
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        if(vrf->IsDeleted()) {
            //Route change, received on deleted VRF
            return;
        }

        if (rt && rt->IsDeleted()) {
            rt->ClearDelete();
        }

        if (data->op_ == Inet4RouteData::CHANGE) {
            // Add route if not present already
            int new_rt = 0;
            if (rt == NULL) {
                rt = new Inet4McRoute(vrf, key->src_, key->addr_);
                assert(rt->GetVrfEntry() != NULL);
                new_rt = 1;
            }
            
            rt->nh_ = GetMcNextHop(key, data); 
            if (new_rt) {
                part->Add(rt);
            } else {
                part->Notify(rt);
            }
        }
    } else {
        if (rt == NULL) {
            return;
        }
        part->Delete(rt);
    }
}

bool Inet4McRouteTable::DelExplicitRoute(DBTablePartBase *part,
                                     DBEntryBase *entry) {

#if 0
    /* Dont Delete the routes as part of VRF delete. 
     * They will be deleted when VmPort Interface is deactivated
     */
    Inet4McRoute *route = static_cast<Inet4McRoute *>(entry);
    if (route && !route->IsDeleted()) {
        part->Delete(route); 
    }
#endif
    return true;
}

Inet4McRoute::Inet4McRoute(VrfEntry *vrf, const Ip4Address &src, const Ip4Address &grp) :
        Inet4Route(vrf, grp, Inet4Route::INET4_MCAST), src_(src) { 
};


int Inet4McRoute::CompareTo(const Route &rhs) const {
    const Inet4McRoute &a = static_cast<const Inet4McRoute &>(rhs);

    if (src_ < a.src_) {
        return -1;
    }

    if (src_ > a.src_) {
        return 1;
    }

    if (GetIpAddress() < a.GetIpAddress()) {
        return -1;
    }

    if (GetIpAddress() > a.GetIpAddress()) {
        return 1;
    }

    return 0;
}

bool Inet4McRoute::IsLess(const DBEntry &rhs) const {
    int cmp = CompareTo(static_cast<const Route &>(rhs));
    return (cmp < 0);
};

string Inet4McRoute::ToString() const {
    return "Inet4McRoute";
}

DBEntryBase::KeyPtr Inet4McRoute::GetDBRequestKey() const {
    Inet4McRouteKey *key = new Inet4McRouteKey(GetVrfEntry()->GetName(), src_, GetIpAddress());
    return DBEntryBase::KeyPtr(key);
}

void Inet4McRoute::SetKey(const DBRequestKey *key) {
    const Inet4McRouteKey *k = static_cast<const Inet4McRouteKey *>(key);
    SetVrf(Inet4McRouteTable::GetInstance()->FindVrfEntry(k->vrf_name_));
    Ip4Address grp(k->addr_);
    SetAddr(grp);
    SetPlen(32);
    Ip4Address src(k->src_);
    src_ = src;
}

void Inet4McRouteTable::AddV4MulticastRoute(const string &vrf_name, 
                         const Ip4Address &src_addr,
                         const Ip4Address &grp_addr) {

    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4McRouteKey *rt_key = new Inet4McRouteKey(vrf_name, src_addr, 
                                                  grp_addr);
    Inet4McastRoute *data = new Inet4McastRoute(src_addr, grp_addr);

    req.key.reset(rt_key);
    req.data.reset(data);
    mc_route_table_->Enqueue(&req);
} 

void Inet4McRouteTable::DeleteV4MulticastRoute(const string &vrf_name, 
                         const Ip4Address &src_addr,
                         const Ip4Address &grp_addr) {
    DBRequest req;

    req.oper = DBRequest::DB_ENTRY_DELETE;
    Inet4McRouteKey *rt_key = new Inet4McRouteKey(vrf_name, src_addr, grp_addr);

    req.key.reset(rt_key);
    req.data.reset(NULL);
    mc_route_table_->Enqueue(&req);
}

Inet4McRoute* Inet4McRouteTable::FindRoute(const string &vrf_name, 
                                       const Ip4Address &src, const Ip4Address &grp) {
        
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    Inet4McRouteTable *rt_table = vrf->GetInet4McRouteTable();
    Inet4McRouteKey *rt_key = new Inet4McRouteKey(vrf_name, src, grp);
    return static_cast<Inet4McRoute *>(rt_table->Find(rt_key));
}       

Inet4Route* Inet4McRouteTable::FindExact(const Ip4Address &src, const Ip4Address &grp) {
        
    Inet4McRouteKey *rt_key = new Inet4McRouteKey(GetVrfName(), src, grp);
    return static_cast<Inet4Route *>(Find(rt_key));
}       

Ip4Address Inet4McRoute::GetSrcIpAddress() const {
    return src_;
}

const NextHop *Inet4McRoute::GetActiveNextHop() const {
    return nh_.get();
}

void Inet4McRouteTable::AddVHostRecvRoute(const string &vm_vrf,
                                        const Ip4Address &addr,
                                        bool policy) {
    ReceiveNH::CreateReq(Agent::GetInstance()->GetVirtualHostInterfaceName());

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4McRouteKey *rt_key = new Inet4McRouteKey(vm_vrf, addr, 32);
    req.key.reset(rt_key);
    VirtualHostInterfaceKey intf_key(nil_uuid(), 
                                     Agent::GetInstance()->GetVirtualHostInterfaceName());
    Inet4McReceiveRoute *data = new Inet4McReceiveRoute(intf_key, policy);
    req.data.reset(data);
    mc_route_table_->Enqueue(&req);
}


void Inet4McRouteTable::DeleteRoute(DBTablePartBase *part,
                                    Inet4McRoute *rt, const Peer *peer) {

    if (rt == NULL) { 
        return; 
    }

#if 0
    //cleanup multicast composite-nhs
    if (rt->IsMcast() &&
        rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE) {

        boost::system::error_code ec;
        CompositeNH::HandleControlNodePeerDown(rt->GetVrfEntry()->GetName(),
                                  rt->GetIpAddress(),
                                  IpAddress::from_string("0.0.0.0", ec).to_v4());
    }
#endif
}


bool Inet4McRouteTable::DelPeerRoutes(DBTablePartBase *part,
                                      DBEntryBase *entry, Peer *peer) {
    Inet4McRoute *route = static_cast<Inet4McRoute *>(entry);
    if (route) {
        DeleteRoute(part, route, peer);
    }   
    return true;
}           

bool Inet4McRoute::Inet4McNotifyRouteEntryWalk(AgentXmppChannel *bgp_xmpp_peer,
                                               DBState *vrf_entry_state,
                                               bool associate,
                                               DBTablePartBase *part,
                                               DBEntryBase *entry) {
    Inet4McRoute *route = static_cast<Inet4McRoute *>(entry);
    VrfExport::State *vs = static_cast<VrfExport::State *>(vrf_entry_state);

    Inet4RouteExport::State *state =
        static_cast<Inet4RouteExport::State *>(route->GetState(part->parent(),
                                               vs->inet4_multicast_export_->GetListenerId()));
    if (state) {
        state->force_chg_ = true;
    }

    vs->inet4_multicast_export_->Notify(bgp_xmpp_peer, associate, part, entry);
    return true;
}

void Inet4McRouteTable::Inet4McRouteTableWalkerNotify(VrfEntry *vrf,
                                                      AgentXmppChannel *bgp_xmpp_peer,
                                                      DBState *state,
                                                      bool associate) {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    if (vrf_state->inet4_mc_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel walk ", "Inet4McRouteTable", 
                           vrf_state->inet4_mc_walkid_,
                           bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                           "Add/Withdraw Route");
        walker->WalkCancel(vrf_state->inet4_mc_walkid_);
    }

    vrf_state->inet4_mc_walkid_ = walker->WalkTable(this, NULL,
        boost::bind(&Inet4McRoute::Inet4McNotifyRouteEntryWalk, bgp_xmpp_peer,
                    state, associate, _1, _2),
        boost::bind(&Inet4McRouteTable::Inet4McRouteNotifyDone, this, _1, state));

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", "Inet4McRouteTable", 
                       vrf_state->inet4_mc_walkid_,
                       bgp_xmpp_peer->GetBgpPeer()->GetName(),
                       (associate)? "Add Route": "Withdraw Route");
}
    
void Inet4McRouteTable::Inet4McRouteNotifyDone(DBTableBase *base,
                                               DBState *state) {
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", "Inet4McRouteTable", 
                       vrf_state->inet4_mc_walkid_, "peer-unknown", 
                       "Add/Del Route");
    vrf_state->inet4_mc_walkid_ = DBTableWalker::kInvalidWalkerId;
}   

void Inet4McRouteTable::DeleteReq(const string &vrf_name, const Ip4Address &addr) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Inet4McRouteKey *key = new Inet4McRouteKey(vrf_name, addr, 32);
    req.key.reset(key);
    req.data.reset(NULL);
    mc_route_table_->Enqueue(&req);
}

bool Inet4McRoute::DBEntrySandesh(Sandesh *sresp) const {
    Inet4McRouteResp *resp = static_cast<Inet4McRouteResp *>(sresp);

    RouteMcSandeshData data;
    data.set_src(GetSrcIpAddress().to_string());
    data.set_grp(GetIpAddress().to_string());
    GetActiveNextHop()->SetNHSandeshData(data.nh);

    std::vector<RouteMcSandeshData> &list = 
        const_cast<std::vector<RouteMcSandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Route Table utility functions to add different kind of routes
/////////////////////////////////////////////////////////////////////////////

void Inet4McRouteReq::HandleRequest() const {
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentInet4McRtSandesh *sand = new AgentInet4McRtSandesh(vrf, context(), "");
    sand->DoSandesh();
}
