/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/address.h>
#include <base/logging.h>
#include <boost/uuid/string_generator.hpp>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_param.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_types.h>

#include <instance_service_server.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/mpls.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <cfg/cfg_types.h>
#include <openstack/instance_service_server.h>
#include <base/contrail_ports.h>
#include <vgw/cfg_vgw.h>
#include <controller/controller_route_path.h>

InstanceServiceAsyncHandler::InstanceServiceAsyncHandler(Agent *agent)
    : io_service_(*(agent->event_manager()->io_service())),
      agent_(agent), version_(0), vgw_version_(0),
      novaPeer_(new Peer(Peer::NOVA_PEER, NOVA_PEER_NAME)),
      interface_stale_cleaner_(new InterfaceConfigStaleCleaner(agent)),
      vgw_stale_cleaner_(new ConfigStaleCleaner(agent, boost::bind(
        &InstanceServiceAsyncHandler::OnVirtualGatewayStaleTimeout, this, _1))) {
    interface_stale_cleaner_->set_callback(
      boost::bind(&InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout,
                  interface_stale_cleaner_.get(), _1));
}

InstanceServiceAsyncHandler::~InstanceServiceAsyncHandler() {
}

InstanceServiceAsyncIf::AddPort_shared_future_t 
InstanceServiceAsyncHandler::AddPort(const PortList& port_list) {
    PortList::const_iterator it;
    for (it = port_list.begin(); it != port_list.end(); ++it) {
        Port port = *it;
        if (port.port_id.size() != (uint32_t)kUuidSize || 
            port.instance_id.size() != (uint32_t)kUuidSize ||
            port.vn_id.size() != (uint32_t)kUuidSize) {
            CFG_TRACE(IntfInfo, 
                      "Port/instance/vn id not valid uuids, size check failed");
            return false;
        }
        uuid port_id = ConvertToUuid(port.port_id);
        uuid instance_id = ConvertToUuid(port.instance_id);
        uuid vn_id = ConvertToUuid(port.vn_id);
        uuid vm_project_id = ConvertToUuid(port.vm_project_id);
        boost::system::error_code ec;
        IpAddress ip = IpAddress::from_string(port.ip_address, ec);
        if (ec.value() != 0) {
            CFG_TRACE(IntfInfo,
                      "IP address is not correct, " + port.ip_address);
            return false;
        }
        
        CfgIntTable *ctable = static_cast<CfgIntTable *>
            (agent_->db()->FindTable("db.cfg_int.0"));
        assert(ctable);

        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key.reset(new CfgIntKey(port_id));
        
        CfgIntData *cfg_int_data = new CfgIntData();
        uint16_t vlan_id = VmInterface::kInvalidVlanId;
        if (port.__isset.vlan_id) {
            vlan_id = port.vlan_id;
        }

        cfg_int_data->Init(instance_id, vn_id, vm_project_id,
                           port.tap_name, ip,
                           port.mac_address,
                           port.display_name, vlan_id, version_);
        req.data.reset(cfg_int_data);
        ctable->Enqueue(&req);
        CFG_TRACE(OpenstackAddPort, "Add", UuidToString(port_id),
                  UuidToString(instance_id), UuidToString(vn_id),
                  port.ip_address, port.tap_name, port.mac_address,
                  port.display_name, port.hostname, port.host, version_,
                  vlan_id, UuidToString(vm_project_id));
    }
    return true;
}

InstanceServiceAsyncIf::KeepAliveCheck_shared_future_t 
InstanceServiceAsyncHandler::KeepAliveCheck() {
    return true;
}

InstanceServiceAsyncIf::Connect_shared_future_t 
InstanceServiceAsyncHandler::Connect() {
    ++version_;
    interface_stale_cleaner_->StartStaleCleanTimer(version_);
    CFG_TRACE(OpenstackConnect, "Connect", version_);
    return true;
}

InstanceServiceAsyncIf::DeletePort_shared_future_t 
InstanceServiceAsyncHandler::DeletePort(const tuuid& t_port_id) {
    uuid port_id = ConvertToUuid(t_port_id);

    CfgIntTable *ctable = static_cast<CfgIntTable *>
        (agent_->db()->FindTable("db.cfg_int.0"));
    assert(ctable);
    
    DBRequest req;
    req.key.reset(new CfgIntKey(port_id));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    CFG_TRACE(OpenstackDeletePort, "Delete", UuidToString(port_id), version_); 
    return true;
}

InstanceServiceAsyncIf::AddVirtualGateway_shared_future_t 
InstanceServiceAsyncHandler::AddVirtualGateway(
                             const VirtualGatewayRequestList& vgw_list) {
    bool ret = true;
    std::vector<VirtualGatewayInfo> vgw_info_list;
    for (VirtualGatewayRequestList::const_iterator it = vgw_list.begin();
         it != vgw_list.end(); ++it) {
        const VirtualGatewayRequest &gw = *it;
        if (!gw.interface_name.size() || !gw.routing_instance.size() ||
            !gw.subnets.size()) {
            LOG(DEBUG, "Add Virtual Gateway : Ignoring invalid gateway configurtion; "
                << "Interface : " << gw.interface_name << " Routing instance : "
                << gw.routing_instance);
            ret = false;
            continue;
        }

        boost::system::error_code ec;
        VirtualGatewayConfig::SubnetList subnets;
        uint32_t i, j;
        for (i = 0; i < gw.subnets.size(); i++) {
            VirtualGatewayConfig::Subnet subnet(Ip4Address::from_string(
                                                gw.subnets[i].prefix, ec),
                                                gw.subnets[i].plen);
            if (ec || gw.subnets[i].plen <= 0 || gw.subnets[i].plen > 32) {
                LOG(DEBUG, "Add Virtual Gateway : Ignoring invalid gateway configurtion; "
                    << "Interface : " << gw.interface_name << " Subnet : "
                    << gw.subnets[i].prefix << "/" << gw.subnets[i].plen);
                break;
            }
            subnets.push_back(subnet);
        }

        VirtualGatewayConfig::SubnetList routes;
        for (j = 0; j < gw.routes.size(); j++) {
            VirtualGatewayConfig::Subnet route(Ip4Address::from_string(
                                               gw.routes[j].prefix, ec),
                                               gw.routes[j].plen);
            if (ec || gw.routes[j].plen < 0 || gw.routes[j].plen > 32) {
                LOG(DEBUG, "Add Virtual Gateway: Ignoring invalid configuration; "
                    << "Interface : " << gw.interface_name << " Subnet : "
                    << gw.routes[j].prefix << "/" << gw.routes[j].plen);
                break;
            }
            routes.push_back(route);
        }

        if (i < gw.subnets.size() || j < gw.routes.size()) {
            ret = false;
            continue;
        }

        vgw_info_list.push_back(VirtualGatewayInfo(gw.interface_name,
                                                   gw.routing_instance,
                                                   subnets, routes));
    }
    if (!vgw_info_list.empty()) {
        boost::shared_ptr<VirtualGatewayData>
            vgw_data(new VirtualGatewayData(VirtualGatewayData::Add,
                                            vgw_info_list, vgw_version_));
        agent_->params()->vgw_config_table()->Enqueue(vgw_data);
    }
    return ret;
}

InstanceServiceAsyncIf::DeleteVirtualGateway_shared_future_t 
InstanceServiceAsyncHandler::DeleteVirtualGateway(
                             const std::vector<std::string>& vgw_list) {
    bool ret = true;
    std::vector<VirtualGatewayInfo> vgw_info_list;
    for (uint32_t i = 0; i < vgw_list.size(); ++i) {
        if (!vgw_list[i].size()) {
            LOG(DEBUG, "Delete Virtual Gateway : Ignoring empty interface name;");
            ret = false;
            continue;
        }
        vgw_info_list.push_back(VirtualGatewayInfo(vgw_list[i]));
    }
    if (!vgw_info_list.empty()) {
        boost::shared_ptr<VirtualGatewayData>
            vgw_data(new VirtualGatewayData(VirtualGatewayData::Delete,
                                            vgw_info_list, vgw_version_));
        agent_->params()->vgw_config_table()->Enqueue(vgw_data);
    }
    return ret;
}

InstanceServiceAsyncIf::ConnectForVirtualGateway_shared_future_t
InstanceServiceAsyncHandler::ConnectForVirtualGateway() {
    ++vgw_version_;
    vgw_stale_cleaner_->StartStaleCleanTimer(vgw_version_);
    return true;
}

InstanceServiceAsyncIf::AuditTimerForVirtualGateway_shared_future_t
InstanceServiceAsyncHandler::AuditTimerForVirtualGateway(int32_t timeout) {
    vgw_stale_cleaner_->set_timeout(timeout);
    return true;
}

void InstanceServiceAsyncHandler::OnVirtualGatewayStaleTimeout(
                                  uint32_t version) {
    // delete older entries (less than "version")
    boost::shared_ptr<VirtualGatewayData>
        vgw_data(new VirtualGatewayData(VirtualGatewayData::Audit, version));
    agent_->params()->vgw_config_table()->Enqueue(vgw_data);
}

InstanceServiceAsyncIf::TunnelNHEntryAdd_shared_future_t 
InstanceServiceAsyncHandler::TunnelNHEntryAdd(const std::string& src_ip, 
					      const std::string& dst_ip, 
					      const std::string& vrf) {

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
    NextHopTable *nh_table_ = static_cast<NextHopTable *>
        (agent_->db()->FindTable("db.nexthop.0"));
    nh_table_->Enqueue(&treq);

    return true;
}


InstanceServiceAsyncIf::TunnelNHEntryDelete_shared_future_t 
InstanceServiceAsyncHandler::TunnelNHEntryDelete(const std::string& src_ip, 
						 const std::string& dst_ip, 
						 const std::string& vrf) {
    LOG(DEBUG, "Source IP Address " << src_ip);
    LOG(DEBUG, "Destination IP Address " << dst_ip);
    LOG(DEBUG, "Vrf " << vrf);
    return true;
}


InstanceServiceAsyncIf::RouteEntryAdd_shared_future_t 
InstanceServiceAsyncHandler::RouteEntryAdd(const std::string& ip_address, 
					   const std::string& gw_ip, 
					   const std::string& vrf,
					   const std::string& label) {
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Gw IP Address " << gw_ip);
    LOG(DEBUG, "Vrf " << vrf);
    LOG(DEBUG, "Label " << label);
    
    IpAddress ip = Ip4Address::from_string(ip_address);
    IpAddress gw = Ip4Address::from_string(gw_ip);

    // IPv4 check
    if (!(ip.is_v4()) || !(gw.is_v4())) {
        LOG(ERROR, "Error < Support only IPv4");
	return false;
    }

    Ip4Address ipv4 = Ip4Address::from_string(ip_address);
    Ip4Address gwv4 = Ip4Address::from_string(gw_ip);

    uint32_t mpls_label;
    const std::string vn = " ";
    sscanf(label.c_str(), "%u", &mpls_label);
    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(agent_->local_peer(),
                                                 agent_->fabric_vrf_name(),
                                                 agent_->router_id(),
                                                 vrf, gwv4,
                                                 TunnelType::AllType(),
                                                 mpls_label,
                                                 vn, SecurityGroupList(),
                                                 PathPreference());
    Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(agent_->local_peer(),
                                     vrf, ipv4, 32, data);
    return true;
}

InstanceServiceAsyncIf::RouteEntryDelete_shared_future_t 
InstanceServiceAsyncHandler::RouteEntryDelete(const std::string& ip_address, 
					      const std::string& vrf) {
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Vrf  " << vrf);
    return true;
}

InstanceServiceAsyncIf::AddHostRoute_shared_future_t 
InstanceServiceAsyncHandler::AddHostRoute(const std::string& ip_address,
					  const std::string& vrf) {
    LOG(DEBUG, "AddHostRoute");
    LOG(DEBUG, "IP Address " << ip_address);
    LOG(DEBUG, "Vrf uuid " << vrf);

    IpAddress ip = IpAddress::from_string(ip_address);
    if (!(ip.is_v4())) {
        LOG(ERROR, "Error < Support only IPv4");
	return false;
    }

    agent_->fabric_inet4_unicast_table()->
        AddHostRoute(vrf, ip.to_v4(), 32, agent_->fabric_vn_name());

    return true;
}

InstanceServiceAsyncIf::AddLocalVmRoute_shared_future_t 
InstanceServiceAsyncHandler::AddLocalVmRoute(const std::string& ip_address,
					     const std::string& intf,
					     const std::string& vrf,
					     const std::string& label) {
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

    agent_->fabric_inet4_unicast_table()->
        AddLocalVmRouteReq(novaPeer_.get(), vrf, ip.to_v4(), 32, intf_uuid, 
                           "instance-service", mpls_label, SecurityGroupList(),
                           false, PathPreference());
    return true;
}

InstanceServiceAsyncIf::AddRemoteVmRoute_shared_future_t
InstanceServiceAsyncHandler::AddRemoteVmRoute(const std::string& ip_address,
                                              const std::string& gw_ip,
                                              const std::string& vrf,
                                              const std::string& label) {
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

    ControllerVmRoute *data =
        ControllerVmRoute::MakeControllerVmRoute(novaPeer_.get(),
                              agent_->fabric_vrf_name(),
                              agent_->router_id(), vrf, gw.to_v4(),
                              TunnelType::AllType(), mpls_label, "",
                              SecurityGroupList(), PathPreference());
    agent_->fabric_inet4_unicast_table()->
        AddRemoteVmRouteReq(novaPeer_.get(),
                            vrf, ip.to_v4(), 32, data);
    return true;
}

InstanceServiceAsyncIf::CreateVrf_shared_future_t 
InstanceServiceAsyncHandler::CreateVrf(const std::string& vrf) {
    LOG(DEBUG, "CreateVrf" << vrf);
    // Vrf
    agent_->vrf_table()->CreateVrf(vrf);
    return true;
}

InstanceServiceAsyncHandler::uuid 
InstanceServiceAsyncHandler::ConvertToUuid(const tuuid &id) {
    boost::uuids::uuid u = nil_uuid();
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

void InstanceInfoServiceServerInit(Agent *agent) {
    boost::shared_ptr<protocol::TProtocolFactory>
        protocolFactory(new protocol::TBinaryProtocolFactory());
    InstanceServiceAsyncHandler *isa = new InstanceServiceAsyncHandler(agent);
    boost::shared_ptr<InstanceServiceAsyncHandler> handler(isa);
    boost::shared_ptr<TProcessor> processor(new InstanceServiceAsyncProcessor(handler));

    boost::shared_ptr<apache::thrift::async::TAsioServer> server(
        new apache::thrift::async::TAsioServer(
            *(agent->event_manager()->io_service()),
            ContrailPorts::NovaVifVrouterAgentPort,
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
    uuid vm_project_uuid = StringToUuid(get_vm_project_uuid());
    string vm_name = get_vm_name();
    string tap_name = get_tap_name();
    uint16_t vlan_id = get_vlan_id();

    boost::system::error_code ec;
    IpAddress ip(IpAddress::from_string(get_ip_address(), ec));
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

    CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
    assert(ctable);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new CfgIntKey(port_uuid));
    CfgIntData *cfg_int_data = new CfgIntData();
    cfg_int_data->Init(instance_uuid, vn_uuid, vm_project_uuid,
                       tap_name, ip,
                       mac_address,
                       vm_name, vlan_id, 0);
    req.data.reset(cfg_int_data);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    ctable->Enqueue(&req);
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

    CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(port_uuid));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    resp->set_resp(std::string("Success"));
    resp->Response();
    return;
}

////////////////////////////////////////////////////////////////////////////////

ConfigStaleCleaner::ConfigStaleCleaner(Agent *agent, TimerCallback callback) :
    agent_(agent),
    timeout_(kConfigStaleTimeout), audit_callback_(callback) {
}

ConfigStaleCleaner::~ConfigStaleCleaner() {
    // clean up the running timers
    for (std::set<Timer *>::iterator it = running_timer_list_.begin();
         it != running_timer_list_.end(); ++it) {
        TimerManager::DeleteTimer(*it);
    }
}

void ConfigStaleCleaner::StartStaleCleanTimer(int32_t version) {
    // create timer, to be deleted on completion
    Timer *timer =
        TimerManager::CreateTimer(
            *(agent_->event_manager())->io_service(), "Stale cleanup timer",
            TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0, true);
    running_timer_list_.insert(timer);
    timer->Start(timeout_,
                 boost::bind(&ConfigStaleCleaner::StaleEntryTimeout, this,
                             version, timer));
}

bool ConfigStaleCleaner::StaleEntryTimeout(int32_t version, Timer *timer) {
    if (audit_callback_) {
        audit_callback_(version);
    }
    running_timer_list_.erase(timer);
    return false;
}

////////////////////////////////////////////////////////////////////////////////

InterfaceConfigStaleCleaner::InterfaceConfigStaleCleaner(Agent *agent) :
    ConfigStaleCleaner(agent, NULL), walkid_(DBTableWalker::kInvalidWalkerId) {
}

InterfaceConfigStaleCleaner::~InterfaceConfigStaleCleaner() {
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk cancelled.");
        agent_->db()->GetWalker()->WalkCancel(walkid_);
    }
}

bool
InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout(int32_t version) {
    DBTableWalker *walker = agent_->db()->GetWalker();
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk cancelled.");
        walker->WalkCancel(walkid_);
    }

    walkid_ = walker->WalkTable(agent_->interface_config_table(), NULL,
              boost::bind(&InterfaceConfigStaleCleaner::CfgIntfWalk, this,
                          _1, _2, version),
              boost::bind(&InterfaceConfigStaleCleaner::CfgIntfWalkDone, this,
                          version));
    CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk invoked.");
    return false;
}

bool InterfaceConfigStaleCleaner::CfgIntfWalk(DBTablePartBase *partition,
                                              DBEntryBase *entry,
                                              int32_t version) {
    const CfgIntEntry *cfg_intf = static_cast<const CfgIntEntry *>(entry);
    if (cfg_intf->GetVersion() < version) {
        CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
        DBRequest req;
        req.key.reset(new CfgIntKey(cfg_intf->GetUuid()));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        ctable->Enqueue(&req);
    }
    return true;
}

void InterfaceConfigStaleCleaner::CfgIntfWalkDone(int32_t version) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    CFG_TRACE(IntfWalkDone, version);
}
