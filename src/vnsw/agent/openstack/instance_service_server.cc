/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "instance_service_server.h"
#include <boost/uuid/string_generator.hpp>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>

#include "oper/interface.h"
#include "oper/nexthop.h"
#include "oper/agent_route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "oper/mirror_table.h"
#include "cfg/cfg_types.h"
#include "cmn/agent_cmn.h"

#include "openstack/instance_service_server.h"
#include "cfg/interface_cfg.h"
#include "cfg/init_config.h"
#include "cfg/cfg_types.h"

#include "net/address.h"


InstanceServiceAsyncIf::AddPort_shared_future_t 
InstanceServiceAsyncHandler::AddPort(const PortList& port_list)
{
    PortList::const_iterator it;
    for (it = port_list.begin(); it != port_list.end(); ++it) {
        Port port = *it;
        if (port.port_id.size() > (uint32_t)kUuidSize || 
            port.instance_id.size() > (uint32_t)kUuidSize) {
            return false;
        }
        uuid port_id = ConvertToUuid(port.port_id);
        uuid instance_id = ConvertToUuid(port.instance_id);
        uuid vn_id = ConvertToUuid(port.vn_id);
        IpAddress ip = IpAddress::from_string(port.ip_address);
        
        CfgIntTable *ctable = static_cast<CfgIntTable *>(db_->FindTable("db.cfg_int.0"));
        assert(ctable);

        DBRequest req;
        req.key.reset(new CfgIntKey(port_id));
        
        CfgIntData *cfg_int_data = new CfgIntData();
        cfg_int_data->Init(instance_id, vn_id, 
                           port.tap_name, ip,
                           port.mac_address,
                           port.display_name, version_);
        req.data.reset(cfg_int_data);
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        ctable->Enqueue(&req);
        CFG_TRACE(OpenstackAddPort, "Add", UuidToString(port_id), UuidToString(instance_id),
                  UuidToString(vn_id), port.ip_address, port.tap_name,
                  port.mac_address, port.display_name, port.hostname,
                  port.host, version_);
    }
    return true;
}

InstanceServiceAsyncIf::KeepAliveCheck_shared_future_t 
InstanceServiceAsyncHandler::KeepAliveCheck()
{
    return true;
}

InstanceServiceAsyncIf::Connect_shared_future_t 
InstanceServiceAsyncHandler::Connect()
{
    ++version_;
    intf_stale_cleaner_->StartStaleCleanTimer(version_);
    CFG_TRACE(OpenstackConnect, "Connect", version_);
    return true;
}

InstanceServiceAsyncIf::DeletePort_shared_future_t 
InstanceServiceAsyncHandler::DeletePort(const tuuid& t_port_id)
{
    uuid port_id = ConvertToUuid(t_port_id);

    CfgIntTable *ctable = static_cast<CfgIntTable *>(db_->FindTable("db.cfg_int.0"));
    assert(ctable);
    
    DBRequest req;
    req.key.reset(new CfgIntKey(port_id));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    CFG_TRACE(OpenstackDeletePort, "Delete", UuidToString(port_id), version_); 
    return true;
}

InstanceServiceAsyncIf::TunnelNHEntryAdd_shared_future_t 
InstanceServiceAsyncHandler::TunnelNHEntryAdd(const std::string& src_ip, 
					      const std::string& dst_ip, 
					      const std::string& vrf)
{

    LOG(DEBUG, "Source IP Address " << src_ip);
    LOG(DEBUG, "Destination IP Address " << dst_ip);
    LOG(DEBUG, "Vrf " << vrf);

    DBRequest treq;    
    Ip4Address sip = Ip4Address::from_string(src_ip);
    Ip4Address dip = Ip4Address::from_string(dst_ip);

    NextHopKey *nh_key = new TunnelNHKey(vrf, sip, dip, false,
                                         TunnelType::DefaultType());
    treq.key.reset(nh_key);
    
    TunnelNHData *nh_data = new TunnelNHData();
    treq.data.reset(nh_data);
    treq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    NextHopTable *nh_table_ = static_cast<NextHopTable *>(db_->FindTable("db.nexthop.0"));
    nh_table_->Enqueue(&treq);

    return true;
}


InstanceServiceAsyncIf::TunnelNHEntryDelete_shared_future_t 
InstanceServiceAsyncHandler::TunnelNHEntryDelete(const std::string& src_ip, 
						 const std::string& dst_ip, 
						 const std::string& vrf)
{
    LOG(DEBUG, "Source IP Address " << src_ip);
    LOG(DEBUG, "Destination IP Address " << dst_ip);
    LOG(DEBUG, "Vrf " << vrf);
    return true;
}


InstanceServiceAsyncIf::RouteEntryAdd_shared_future_t 
InstanceServiceAsyncHandler::RouteEntryAdd(const std::string& ip_address, 
					   const std::string& gw_ip, 
					   const std::string& vrf,
					   const std::string& label)
{

    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Gw IP Address " << gw_ip);
    LOG(DEBUG, "Vrf " << vrf);
    LOG(DEBUG, "Label " << label);
    
    IpAddress ip = IpAddress::from_string(ip_address);
    IpAddress gw = IpAddress::from_string(gw_ip);

    // IPv4 check
    if (!(ip.is_v4()) || !(gw.is_v4())) {
        LOG(ERROR, "Error < Support only IPv4");
	return false;
    }

    // Vrf
    DBRequest rreq;
    Inet4UnicastRouteKey *rt_key = new Inet4UnicastRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                              vrf, ip.to_v4(), 32);    
    rreq.key.reset(rt_key);

    uint32_t mpls_label;
    SecurityGroupList sg_list;
    sscanf(label.c_str(), "%u", &mpls_label);
    RemoteVmRoute *data = new RemoteVmRoute(Agent::GetInstance()->GetDefaultVrf(),
                                            gw.to_v4(), mpls_label, "",
                                            TunnelType::AllType(), sg_list);
    rreq.data.reset(data);
    rreq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastAgentRouteTable *rt_table_ = 
        static_cast<Inet4UnicastAgentRouteTable *>(db_->FindTable("db.uc.route.0"));
    rt_table_->Enqueue(&rreq);
    return true;
}

InstanceServiceAsyncIf::RouteEntryDelete_shared_future_t 
InstanceServiceAsyncHandler::RouteEntryDelete(const std::string& ip_address, 
					      const std::string& vrf)
{
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Vrf  " << vrf);
    return true;
}

InstanceServiceAsyncIf::AddHostRoute_shared_future_t 
InstanceServiceAsyncHandler::AddHostRoute(const std::string& ip_address,
					  const std::string& vrf)
{
    LOG(DEBUG, "AddHostRoute");
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Vrf uuid " << vrf);

    IpAddress ip = IpAddress::from_string(ip_address);
    if (!(ip.is_v4())) {
        LOG(ERROR, "Error < Support only IPv4");
	return false;
    }

    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddHostRoute(vrf, ip.to_v4(), 32, 
                                                       Agent::GetInstance()->GetFabricVnName());

    return true;
}

InstanceServiceAsyncIf::AddLocalVmRoute_shared_future_t 
InstanceServiceAsyncHandler::AddLocalVmRoute(const std::string& ip_address,
					     const std::string& intf,
					     const std::string& vrf,
					     const std::string& label)
{
    LOG(DEBUG, "AddLocalVmRoute");
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Intf uuid " << intf);
    LOG(DEBUG, "Vrf " << vrf);
    LOG(DEBUG, "Label " << label);

    IpAddress ip = IpAddress::from_string(ip_address);
    if (!(ip.is_v4())) {
        LOG(ERROR, "Error < Support only IPv4");
	return false;
    }

    if (intf.empty()) {
        LOG(ERROR, "Error < Intf uuid is required");
	return false;
    }
    boost::uuids::uuid intf_uuid;
    boost::uuids::string_generator gen;
    intf_uuid = gen(intf);

    uint32_t mpls_label;
    if (label.empty()) {
        mpls_label = 0;
    } else {
        sscanf(label.c_str(), "%u", &mpls_label);
    }

    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddLocalVmRoute(novaPeer_, 
                                                        vrf, ip.to_v4(), 32,
                                                        intf_uuid, "instance-service",
                                                        mpls_label);

    return true;
}

InstanceServiceAsyncIf::AddRemoteVmRoute_shared_future_t
InstanceServiceAsyncHandler::AddRemoteVmRoute(const std::string& ip_address,
					      const std::string& gw_ip,
					      const std::string& vrf,
					      const std::string& label)
{
    LOG(DEBUG, "AddRemoteVmRoute");
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Gw IP Address " << gw_ip);
    LOG(DEBUG, "Vrf " << vrf);
    LOG(DEBUG, "Label " << label);

    IpAddress ip = IpAddress::from_string(ip_address);
    IpAddress gw = IpAddress::from_string(gw_ip);

    // IPv4 check
    if (!(ip.is_v4()) || !(gw.is_v4())) {
        LOG(ERROR, "Error < Support only IPv4");
	return false;
    }

    uint32_t mpls_label;
    if (label.empty()) {
        mpls_label = 0;
    } else {
        sscanf(label.c_str(), "%u", &mpls_label);
    }

    Agent::GetInstance()->GetDefaultInet4UnicastRouteTable()->AddRemoteVmRoute(novaPeer_, vrf, 
                                                  ip.to_v4(), 32,
                                                  gw.to_v4(),
                                                  TunnelType::AllType(), 
                                                  mpls_label, "");
    return true;
}

InstanceServiceAsyncIf::CreateVrf_shared_future_t 
InstanceServiceAsyncHandler::CreateVrf(const std::string& vrf)
{
    LOG(DEBUG, "CreateVrf" << vrf);
    // Vrf
    Agent::GetInstance()->GetVrfTable()->CreateVrf(vrf);
    return true;
}

InstanceServiceAsyncHandler::uuid 
InstanceServiceAsyncHandler::ConvertToUuid(const tuuid &id)
{
    boost::uuids::uuid u;
    std::vector<int16_t>::const_iterator it;
    int i;
    
    for (it = id.begin(), i = 0;
         it != id.end() && i < kUuidSize;
         it++, i++) {
      u.data[i] = (uint8_t)*it;
    }
    return u;
}

InstanceServiceAsyncHandler::uuid 
InstanceServiceAsyncHandler::MakeUuid(int id) {
    char str[50];
    boost::uuids::string_generator gen;
    sprintf(str, "0000000000000000000000000000000%d", id);
    boost::uuids::uuid u1 = gen(std::string(str));

    return u1;
}

void InstanceServiceAsyncHandler::SetDb(DB *db)
{
    db_ = db;
}

void InstanceServiceAsyncHandler::SetCfgIntfStaleCleaner(CfgIntfStaleCleaner *intf_stale_cleaner)
{
    intf_stale_cleaner_ = intf_stale_cleaner;
}

void InstanceInfoServiceServerInit(EventManager &evm, DB *db) {
  boost::shared_ptr<protocol::TProtocolFactory> protocolFactory(new protocol::TBinaryProtocolFactory());
  InstanceServiceAsyncHandler *isa = new InstanceServiceAsyncHandler(*evm.io_service());
  isa->SetDb(db);
  CfgIntfStaleCleaner *intf_stale_cleaner = new CfgIntfStaleCleaner(db, 
                                                   *(Agent::GetInstance()->GetEventManager())->io_service());
  isa->SetCfgIntfStaleCleaner(intf_stale_cleaner); 
  boost::shared_ptr<InstanceServiceAsyncHandler> handler(isa);
  boost::shared_ptr<TProcessor> processor(new InstanceServiceAsyncProcessor(handler));

  boost::shared_ptr<apache::thrift::async::TAsioServer> server(
      new apache::thrift::async::TAsioServer(
          *evm.io_service(),
          9090,
          protocolFactory,
          protocolFactory,
          processor));

  server->start(); // Nonblocking

}

static bool ValidateMac(std::string &mac) {
    size_t pos = 0;
    int colon = 0;
    if (mac.empty()) {
        return false;
    }
    while ((pos = mac.find(':', pos)) != std::string::npos) {
        colon++;
        pos += 1;
    }
    if (colon != 5)
        return false;
    return true;
}

void AddPortReq::HandleRequest() const {
    bool err = false;
    string resp_str;

    PortResp *resp = new PortResp();
    resp->set_context(context());

    uuid port_uuid = StringToUuid(get_port_uuid());
    uuid instance_uuid = StringToUuid(get_instance_uuid());
    uuid vn_uuid = StringToUuid(get_vn_uuid());
    string vm_name = get_vm_name();
    string tap_name = get_tap_name();
    boost::system::error_code ec;
    IpAddress ip(boost::asio::ip::address::from_string(get_ip_address(), ec));
    string mac_address = get_mac_address();

    if (port_uuid == nil_uuid()) {
        resp_str += "Port uuid is not correct, ";
        err = true;
    }
    if (instance_uuid == nil_uuid()) {
        resp_str += "Instance uuid is not correct, ";
        err = true;
    }
    if (vn_uuid == nil_uuid()) {
        resp_str += "Vn uuid is not correct, ";
        err = true;
    }
    if (ec != 0) {
        resp_str += "Ip address is not correct, ";
        err = true;
    }
    if (!ValidateMac(mac_address)) {
        resp_str += "Invalid MAC, Use xx:xx:xx:xx:xx:xx format";
        err = true;
    }
    
    if (err) {
        resp->set_resp(resp_str);
        resp->Response();
        return;
    }

    CfgIntTable *ctable = Agent::GetInstance()->GetIntfCfgTable();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(port_uuid));
    CfgIntData *cfg_int_data = new CfgIntData();
    cfg_int_data->Init(instance_uuid, vn_uuid,
                       tap_name, ip,
                       mac_address,
                       vm_name, 0);
    req.data.reset(cfg_int_data);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    resp->set_resp(std::string("Success"));
    resp->Response();
    return;
}

void DeletePortReq::HandleRequest() const {
    string_generator gen;

    PortResp *resp = new PortResp();
    resp->set_context(context());

    uuid port_uuid = StringToUuid(get_port_uuid());
    if (port_uuid == nil_uuid()) {
        resp->set_resp(std::string("Port uuid is not correct."));
        resp->Response();
    }

    CfgIntTable *ctable = Agent::GetInstance()->GetIntfCfgTable();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(port_uuid));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    resp->set_resp(std::string("Success"));
    resp->Response();
    return;
}

CfgIntfStaleCleaner::~CfgIntfStaleCleaner() {
    TimerManager::DeleteTimer(timer_);
}

CfgIntfStaleCleaner::CfgIntfStaleCleaner(DB *db, boost::asio::io_service &io_service) :
        db_(db), timer_(TimerManager::CreateTimer(io_service,
                                                  "Interface Stale cleanup timer")),
        walkid_(DBTableWalker::kInvalidWalkerId) {
            
}

bool CfgIntfStaleCleaner::CfgIntfWalk(DBTablePartBase *partition,
                                          DBEntryBase *entry, int32_t version) {
    const CfgIntEntry *cfg_intf = static_cast<const CfgIntEntry *>(entry);
    if (cfg_intf->GetVersion() < version) {
        CfgIntTable *ctable = Agent::GetInstance()->GetIntfCfgTable();
        DBRequest req;
        req.key.reset(new CfgIntKey(cfg_intf->GetUuid()));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        ctable->Enqueue(&req);
    }
    return true;
}

void CfgIntfStaleCleaner::CfgIntfWalkDone(int32_t version) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    CFG_TRACE(IntfWalkDone, version);
}

bool CfgIntfStaleCleaner::StaleEntryTimeout(int32_t version)
{
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        CFG_TRACE(IntfInfo, "Walk canceled.")
        walker->WalkCancel(walkid_);
    }

    walkid_ = walker->WalkTable(Agent::GetInstance()->GetIntfCfgTable(), NULL,
                      boost::bind(&CfgIntfStaleCleaner::CfgIntfWalk, this, _1, _2, version),
                      boost::bind(&CfgIntfStaleCleaner::CfgIntfWalkDone, this, version));
    CFG_TRACE(IntfInfo, "Walk invoked.");
    return false;
}

void CfgIntfStaleCleaner::StartStaleCleanTimer(int32_t version)
{
    if (timer_->running()) {
        timer_->Cancel();
    }
    CFG_TRACE(IntfWalkStart, kCfgIntfStaleTimeout/1000, version);
    timer_->Start(kCfgIntfStaleTimeout,
                  boost::bind(&CfgIntfStaleCleaner::StaleEntryTimeout,
                              this, version));
}

