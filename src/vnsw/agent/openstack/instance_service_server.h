/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_INSTANCE_SERVICE_SERVER_H_
#define __AGENT_INSTANCE_SERVICE_SERVER_H_

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <protocol/TBinaryProtocol.h>
#include <async/TAsioAsync.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif
#include <async/TFuture.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "gen-cpp/InstanceService.h"
#include <oper/peer.h>
#include <port_ipc/config_stale_cleaner.h>

using namespace apache::thrift;

class InstanceServiceAsyncHandler: virtual public InstanceServiceAsyncIf {
public:
    static const int kUuidSize = 16;
    typedef boost::uuids::uuid uuid;

    InstanceServiceAsyncHandler(Agent *agent);
    ~InstanceServiceAsyncHandler();

    virtual AddPort_shared_future_t AddPort(const PortList& port_list);
    virtual KeepAliveCheck_shared_future_t KeepAliveCheck();
    virtual Connect_shared_future_t Connect();
    virtual DeletePort_shared_future_t DeletePort(const tuuid& port_id);

    virtual AddVirtualGateway_shared_future_t AddVirtualGateway(
                             const VirtualGatewayRequestList& vgw_list);
    virtual DeleteVirtualGateway_shared_future_t DeleteVirtualGateway(
                             const std::vector<std::string>& vgw_list);
    virtual ConnectForVirtualGateway_shared_future_t ConnectForVirtualGateway();
    virtual AuditTimerForVirtualGateway_shared_future_t
                AuditTimerForVirtualGateway(int32_t timeout);
    void OnVirtualGatewayStaleTimeout(uint32_t version);

    virtual TunnelNHEntryAdd_shared_future_t TunnelNHEntryAdd(
						     const std::string& src_ip, 
						     const std::string& dst_ip, 
						     const std::string& vrf_name);
    virtual TunnelNHEntryDelete_shared_future_t TunnelNHEntryDelete(
						     const std::string& src_ip, 
						     const std::string& dst_ip, 
						     const std::string& vrf_name);

    virtual RouteEntryAdd_shared_future_t RouteEntryAdd(
						     const std::string& ip_address, 
						     const std::string& gw_ip, 
						     const std::string& vrf_name,
						     const std::string& label);
    virtual RouteEntryDelete_shared_future_t RouteEntryDelete(
						     const std::string& ip_address, 
						     const std::string& vrf_name);


    virtual AddHostRoute_shared_future_t AddHostRoute(
						     const std::string& ip_address,
						     const std::string& vrf_name);
    virtual AddLocalVmRoute_shared_future_t AddLocalVmRoute(
						     const std::string& ip_address,
						     const std::string& intf_uuid,
						     const std::string& vrf_name,
						     const std::string& label);
    virtual AddRemoteVmRoute_shared_future_t AddRemoteVmRoute(
						     const std::string& ip_address,
						     const std::string& gw_ip,
						     const std::string& vrf_name,
						     const std::string& label);

    virtual CreateVrf_shared_future_t CreateVrf(const std::string& vrf_name);
    uuid ConvertToUuid(const tuuid &tid);

    int version() const { return version_; }
protected:
    boost::asio::io_service& io_service_;

private:
    friend class InstanceServiceTest;
    InterfaceConfigStaleCleaner *interface_stale_cleaner() const {
        return interface_stale_cleaner_.get();
    }
    ConfigStaleCleaner *vgw_stale_cleaner() const {
        return vgw_stale_cleaner_.get();
    }

    uuid MakeUuid(int id);

    Agent *agent_;
    int version_;
    int vgw_version_;
    boost::scoped_ptr<Peer> novaPeer_;
    boost::scoped_ptr<InterfaceConfigStaleCleaner> interface_stale_cleaner_;
    boost::scoped_ptr<ConfigStaleCleaner> vgw_stale_cleaner_;
};

void InstanceInfoServiceServerInit(Agent *agent);

#endif /* __AGENT_INSTANCE_SERVICE_SERVER_H_ */
