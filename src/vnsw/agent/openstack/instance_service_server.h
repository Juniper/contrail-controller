/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_INSTANCE_SERVICE_SERVER_H_
#define __AGENT_INSTANCE_SERVICE_SERVER_H_

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>
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
#include "db/db.h"
#include "io/event_manager.h"
#include "oper/peer.h"

using namespace apache::thrift;

class InstanceServiceAsyncHandler: virtual public InstanceServiceAsyncIf {
public:
    static const int kUuidSize = 16;
    typedef boost::uuids::uuid uuid;

    InstanceServiceAsyncHandler(boost::asio::io_service& io_service) : io_service_(io_service) {
        novaPeer_ = new Peer(Peer::NOVA_PEER, NOVA_PEER_NAME); 
    }
    ~InstanceServiceAsyncHandler() {
        delete novaPeer_;
    }
    virtual AddPort_shared_future_t AddPort(const PortList& port_list);
    virtual KeepAliveCheck_shared_future_t KeepAliveCheck();
    virtual DeletePort_shared_future_t DeletePort(const tuuid& port_id);

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

    void SetDb(DB *db);
protected:
    boost::asio::io_service& io_service_;
    
private:
    uuid ConvertToUuid(const tuuid &tid);
    void PrintUuid(const uuid &id);
    uuid MakeUuid(int id);
    Peer *novaPeer_;
    DB *db_;
};

void InstanceInfoServiceServerInit(EventManager &evm, DB *db);

#endif /* __AGENT_INSTANCE_SERVICE_SERVER_H_ */
