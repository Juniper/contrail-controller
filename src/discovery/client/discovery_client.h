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
    DiscoveryServiceClient *ds_client_;
    std::string publish_msg_;
    std::string publish_hdr_;
    int attempts_;

    // Stats
    int pub_sent_;
    int pub_rcvd_;
    int pub_fail_;
    int pub_fallback_;
    int pub_hb_sent_;
    int pub_hb_fail_;
    int pub_hb_rcvd_;

    bool publish_cb_called_;
    bool heartbeat_cb_called_;
};

typedef boost::function<void()> EnqueuedCb;
class DiscoveryServiceClient {
public:
    static const char *IFMapService;
    static const char *XmppService;
    static const char *CollectorService;
    static const char *DNSService;

    DiscoveryServiceClient(EventManager *evm, boost::asio::ip::tcp::endpoint,
                           std::string client_name);
    virtual ~DiscoveryServiceClient();
    
    void Init();
    void Shutdown();

    void Publish(std::string serviceName, std::string &msg);
    void PublishResponseHandler(std::string &msg, boost::system::error_code, 
                                std::string serviceName, HttpConnection *);
    void WithdrawPublish(std::string serviceName);

    typedef boost::function<void(std::vector<DSResponse>)> ServiceHandler;
    void Subscribe(std::string serviceName, 
                   uint8_t numbOfInstances, ServiceHandler);
    void SubscribeResponseHandler(std::string &msg, boost::system::error_code &, 
                                  std::string serviceName, HttpConnection *);

    void Unsubscribe(std::string serviceName);

    void SendHeartBeat(std::string serviceName, std::string msg);
    void HeartBeatResponseHandler(std::string &msg, boost::system::error_code, 
                                  std::string serviceName, HttpConnection *);

    DSPublishResponse *GetPublishResponse(std::string serviceName);

    // sandesh introspect fill stats 
    void FillDiscoveryServicePublisherStats(
         std::vector<DiscoveryClientPublisherStats> &ds_stats); 

    void FillDiscoveryServiceSubscriberStats(
         std::vector<DiscoveryClientSubscriberStats> &ds_stats); 

    // Map of <ServiceName, SubscribeResponseHeader> for subscribe
    typedef std::map<std::string, DSResponseHeader *> ServiceResponseMap;

    // Map of <ServiceName, PublishResponseHeader> for publish
    typedef std::map<std::string, DSPublishResponse *> PublishResponseMap;

    boost::asio::ip::tcp::endpoint GetDSServerEndPoint() {
        return ds_endpoint_;
    }

    ServiceResponseMap service_response_map_;
    PublishResponseMap publish_response_map_;

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

    HttpClient *http_client_;
    EventManager *evm_;
    boost::asio::ip::tcp::endpoint ds_endpoint_;

    void Publish(std::string serviceName);
    void WithdrawPublishInternal(std::string serviceName);
    void Subscribe(std::string serviceName, uint8_t numbOfInstances);
    void UnsubscribeInternal(std::string serviceName);

    bool DequeueEvent(EnqueuedCb);
    WorkQueue<EnqueuedCb> work_queue_;
    bool shutdown_;

    std::string subscriber_name_;
};

#endif  // __DISCOVERY_SERVICE_CLIENT_H__
