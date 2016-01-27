/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __DISCOVERY_SERVICE_CLIENT_H__
#define __DISCOVERY_SERVICE_CLIENT_H__

#include <map>
#include <string>
#include <iostream>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/function.hpp>
#include <sandesh/sandesh_trace.h>

#include "io/event_manager.h"
#include "base/timer.h"

#include "http/client/http_client.h"
#include "http/client/http_curl.h"

class ServiceType;
class DiscoveryServiceClient;
class DiscoveryServiceClientMock;
struct DiscoveryClientPublisherStats;
struct DiscoveryClientSubscriberStats;

struct DSResponse {
   boost::asio::ip::tcp::endpoint  ep;
};

struct DSResponseHeader {
    DSResponseHeader(std::string serviceName, uint8_t numbOfInstances, 
                     EventManager *evm, DiscoveryServiceClient *);
    ~DSResponseHeader();

    bool SubscribeTimerExpired();
    void StartSubscribeTimer(int);

    int GetConnectTime() const;

    /* Subscribe Request cached */
    std::string serviceName_;
    uint8_t numbOfInstances_;

    /* Subscribe Response cached */
    uint32_t chksum_;
    Timer *subscribe_timer_; 
    std::vector<DSResponse> service_list_;   
    DiscoveryServiceClient *ds_client_;
    std::string subscribe_msg_;
    int attempts_;

    // Stats
    int sub_sent_;
    int sub_rcvd_;
    int sub_fail_;

    bool subscribe_cb_called_;
};

struct DSPublishResponse {
    DSPublishResponse(std::string serviceName, EventManager *evm,
                      DiscoveryServiceClient *); 
    ~DSPublishResponse();

    //udp heart-beat async response
    void DSPublishHeartBeatResponse(const boost::system::error_code &, size_t);

    bool HeartBeatTimerExpired();
    void StartHeartBeatTimer(int);
    void StopHeartBeatTimer();

    bool PublishConnectTimerExpired();
    void StartPublishConnectTimer(int);
    void StopPublishConnectTimer();

    int GetConnectTime() const;

    std::string serviceName_;

    /* HeartBeat publisher cookie */
    std::string cookie_;
    /* HeartBeat Timer */
    Timer *publish_hb_timer_;
    /* Connect Timer */
    Timer *publish_conn_timer_;

    boost::asio::ip::udp::endpoint dss_ep_;
    bool oper_state;
    std::string oper_state_reason;
    DiscoveryServiceClient *ds_client_;
    std::string publish_msg_;
    std::string publish_hdr_;
    std::string client_msg_;
    int attempts_;

    // Stats
    int pub_sent_;
    int pub_rcvd_;
    int pub_fail_;
    int pub_fallback_;
    int pub_hb_sent_;
    int pub_hb_fail_;
    int pub_hb_rcvd_;
    int pub_hb_timeout_;

    bool publish_cb_called_;
    bool heartbeat_cb_called_;
};

typedef boost::function<void()> EnqueuedCb;
class DiscoveryServiceClient {
public:
    static const int kHeartBeatInterval = 5;

    DiscoveryServiceClient(EventManager *evm, boost::asio::ip::tcp::endpoint,
                           std::string client_name,
                           std::string reeval_publish_taskname="bgp::Config");
    virtual ~DiscoveryServiceClient();
    
    void Init();
    void Shutdown();

    static bool ParseDiscoveryServerConfig(std::string discovery_server,
                uint16_t port, boost::asio::ip::tcp::endpoint *);

    typedef boost::function<bool(std::string&)> ReEvalPublishCbHandler;
    void Publish(std::string serviceName, std::string &msg,
                 ReEvalPublishCbHandler);
    void Publish(std::string serviceName, std::string &msg);
    void PublishResponseHandler(std::string &msg, boost::system::error_code, 
                                std::string serviceName, HttpConnection *);
    void WithdrawPublish(std::string serviceName);

    typedef boost::function<void(std::vector<DSResponse>)> ServiceHandler;
    void Subscribe(std::string serviceName, 
                   uint8_t numbOfInstances, ServiceHandler);
    void Subscribe(std::string serviceName, uint8_t numbOfInstances);
    void SubscribeResponseHandler(std::string &msg, boost::system::error_code &, 
                                  std::string serviceName, HttpConnection *);

    void Unsubscribe(std::string serviceName);

    virtual void SendHeartBeat(std::string serviceName, std::string msg);
    void HeartBeatResponseHandler(std::string &msg, boost::system::error_code, 
                                  std::string serviceName, HttpConnection *);

    DSPublishResponse *GetPublishResponse(std::string serviceName);

    void SetHeartBeatInterval(int seconds) {
        heartbeat_interval_ = seconds;
    }
    int GetHeartBeatInterval() { return heartbeat_interval_; }

    // Test Functions
    bool IsPublishServiceRegisteredUp(std::string serviceName);
    void PublishServiceReEvalString(std::string serviceName,
                                    std::string &reeval_reason);

    // sandesh introspect fill stats 
    void FillDiscoveryServicePublisherStats(
         std::vector<DiscoveryClientPublisherStats> &ds_stats); 

    void FillDiscoveryServiceSubscriberStats(
         std::vector<DiscoveryClientSubscriberStats> &ds_stats); 

    // Map of <ServiceName, PublishResponseHeader> for publish
    typedef std::map<std::string, DSPublishResponse *> PublishResponseMap;
    // Map of <ServiceName, SubscribeResponseHeader> for subscribe
    typedef std::map<std::string, DSResponseHeader *> ServiceResponseMap;
    // Map of <ServiceName, ReEvalPublishCbHandler> for reeval publish
    typedef std::map<std::string, ReEvalPublishCbHandler> ReEvalPublishCbHandlerMap;

    boost::asio::ip::tcp::endpoint GetDSServerEndPoint() {
        return ds_endpoint_;
    }

    ServiceResponseMap service_response_map_;
    PublishResponseMap publish_response_map_;
    ReEvalPublishCbHandlerMap reeval_publish_map_;

private:
    friend struct DSResponseHeader;
    friend struct DSPublishResponse;

    //Build and send http post message
    void SendHttpPostMessage(std::string, std::string, std::string);

    // Application specific response handler cb
    typedef std::map<std::string, ServiceHandler>SubscribeResponseHandlerMap;
    void RegisterSubscribeResponseHandler(std::string serviceName, ServiceHandler);
    void UnRegisterSubscribeResponseHandler(std::string serviceName);
    SubscribeResponseHandlerMap subscribe_map_;

    // Application specific ReEvalPublish cb handler
    void RegisterReEvalPublishCbHandler(std::string serviceName,
                                        ReEvalPublishCbHandler);

    HttpClient *http_client_;
    EventManager *evm_;
    boost::asio::ip::tcp::endpoint ds_endpoint_;

    void Publish(std::string serviceName);
    void ReEvaluatePublish(std::string serviceName, ReEvalPublishCbHandler);
    void WithdrawPublishInternal(std::string serviceName);
    void UnsubscribeInternal(std::string serviceName);

    bool DequeueEvent(EnqueuedCb);
    WorkQueue<EnqueuedCb> work_queue_;

    bool ReEvalautePublishCbDequeueEvent(EnqueuedCb);
    WorkQueue<EnqueuedCb>reevaluate_publish_cb_queue_;

    bool shutdown_;
    std::string subscriber_name_;
    int heartbeat_interval_;
};

#endif  // __DISCOVERY_SERVICE_CLIENT_H__
