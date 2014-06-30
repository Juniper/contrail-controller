/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "base/logging.h"
#include <base/connection_info.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/common/vns_constants.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/bind.hpp>
#include <boost/functional/hash.hpp>
#include <discovery/client/discovery_client_types.h>
#include <discovery/client/discovery_client_stats_types.h>

#include "discovery_client_priv.h"
#include "discovery_client.h"
#include "xml/xml_base.h"                                                             
#include "xml/xml_pugi.h"                                                                              

using namespace std; 
namespace ip = boost::asio::ip;

const char *DiscoveryServiceClient::XmppService = "xmpp-server";
const char *DiscoveryServiceClient::CollectorService = "collector-server";
const char *DiscoveryServiceClient::DNSService = "dns-server";

SandeshTraceBufferPtr DiscoveryClientTraceBuf(SandeshTraceBufferCreate(
    "DiscoveryClient", 1000));

/*************** Discovery Service Subscribe Response Message Header **********/
DSResponseHeader::DSResponseHeader(std::string serviceName, uint8_t numbOfInstances, 
                                   EventManager *evm,
                                   DiscoveryServiceClient *ds_client) 
    : serviceName_(serviceName), numbOfInstances_(numbOfInstances),
      chksum_(0),
      subscribe_timer_(TimerManager::CreateTimer(*evm->io_service(), "Subscribe Timer",
                       TaskScheduler::GetInstance()->GetTaskId("http client"), 0)),
      ds_client_(ds_client), subscribe_msg_(""), attempts_(0),
      sub_sent_(0), sub_rcvd_(0), sub_fail_(0),
      subscribe_cb_called_(false) {
}

DSResponseHeader::~DSResponseHeader() {
    service_list_.clear();
    TimerManager::DeleteTimer(subscribe_timer_);
}

int DSResponseHeader::GetConnectTime() const {
    int backoff = min(attempts_, 6);
    // kConnectInterval = 30secs 
    return std::min(backoff ? 1 << (backoff - 1) : 0, 30);
}

bool DSResponseHeader::SubscribeTimerExpired() {
    // Resend subscription request
    ds_client_->Subscribe(serviceName_, numbOfInstances_); 
    return false;
}

void DSResponseHeader::StartSubscribeTimer(int seconds) {
    subscribe_timer_->Cancel(); 
    subscribe_timer_->Start(seconds * 1000,
        boost::bind(&DSResponseHeader::SubscribeTimerExpired, this));
}

/**************** Discovery Service Publish Response Message ******************/
DSPublishResponse::DSPublishResponse(std::string serviceName, 
                                     EventManager *evm,
                                     DiscoveryServiceClient *ds_client)
    : serviceName_(serviceName), 
      publish_hb_timer_(TimerManager::CreateTimer(*evm->io_service(), "Publish Timer",
                        TaskScheduler::GetInstance()->GetTaskId("http client"), 0)),
      publish_conn_timer_(TimerManager::CreateTimer(*evm->io_service(), "Publish Conn Timer",
                          TaskScheduler::GetInstance()->GetTaskId("http client"), 0)),
      ds_client_(ds_client), publish_msg_(""), attempts_(0),
      pub_sent_(0), pub_rcvd_(0), pub_fail_(0), pub_fallback_(0),
      pub_hb_sent_(0), pub_hb_fail_(0), pub_hb_rcvd_(0),
      publish_cb_called_(false), heartbeat_cb_called_(false) {
}

DSPublishResponse::~DSPublishResponse() {
    assert(publish_conn_timer_->cancelled() == true);
    assert(publish_hb_timer_->cancelled() == true);

    TimerManager::DeleteTimer(publish_hb_timer_);
    TimerManager::DeleteTimer(publish_conn_timer_);
}

bool DSPublishResponse::HeartBeatTimerExpired() {
    stringstream hb;
    hb.clear();
    hb << "<cookie>" << cookie_ << "</cookie>" ;
    ds_client_->SendHeartBeat(serviceName_, hb.str());
    //
    // Start the timer again, by returning true
    //
    return true;
}

void DSPublishResponse::StartHeartBeatTimer(int seconds) {
    publish_hb_timer_->Cancel(); 
    publish_hb_timer_->Start(seconds * 1000,
        boost::bind(&DSPublishResponse::HeartBeatTimerExpired, this));
}

void DSPublishResponse::StopHeartBeatTimer() {
    publish_hb_timer_->Cancel();
}

int DSPublishResponse::GetConnectTime() const {
    int backoff = min(attempts_, 6);
    // kConnectInterval = 30secs 
    return std::min(backoff ? 1 << (backoff - 1) : 0, 30);
}

bool DSPublishResponse::PublishConnectTimerExpired() {
    // Resend subscription request
    ds_client_->Publish(serviceName_); 
    return false;
}

void DSPublishResponse::StopPublishConnectTimer() {
    publish_conn_timer_->Cancel();
}

void DSPublishResponse::StartPublishConnectTimer(int seconds) {
    // TODO lock needed??
    StopPublishConnectTimer();
    publish_conn_timer_->Start(seconds * 1000,
        boost::bind(&DSPublishResponse::PublishConnectTimerExpired, this));
}

static void WaitForIdle() {
    static const int kTimeout = 15;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }    
}

/******************* DiscoveryServiceClient ************************************/
DiscoveryServiceClient::DiscoveryServiceClient(EventManager *evm,
                                               boost::asio::ip::tcp::endpoint ep,
                                               std::string client_name) 
    : http_client_(new HttpClient(evm)),
      evm_(evm), ds_endpoint_(ep), 
      work_queue_(TaskScheduler::GetInstance()->GetTaskId("http client"), 0,
                  boost::bind(&DiscoveryServiceClient::DequeueEvent, this, _1)),
      shutdown_(false),
      subscriber_name_(client_name)  {
}

void DiscoveryServiceClient::Init() {
    http_client_->Init();
}

void DiscoveryServiceClient::Shutdown() {
    // Cleanup subscribed services
    for (ServiceResponseMap::const_iterator iter = service_response_map_.begin(), next = iter;
         iter != service_response_map_.end(); iter = next)  {
         next++;
         Unsubscribe(iter->first);
    }
         
    // Cleanup published services 
    for (PublishResponseMap::const_iterator iter = publish_response_map_.begin(), next = iter;
         iter != publish_response_map_.end(); iter = next)  {
         next++;
         WithdrawPublish(iter->first);
    }

    http_client_->Shutdown();
    // Make sure that the above enqueues on the work queue are processed
    WaitForIdle();
    shutdown_ = true;
}

DiscoveryServiceClient::~DiscoveryServiceClient() {
    assert(shutdown_);
    work_queue_.Shutdown();
    TcpServerManager::DeleteServer(http_client_);
} 

bool DiscoveryServiceClient::DequeueEvent(EnqueuedCb cb) {
    cb();
    return true;
}

void DiscoveryServiceClient::PublishResponseHandler(std::string &xmls, 
                                                    boost::system::error_code ec, 
                                                    std::string serviceName,
                                                    HttpConnection *conn) {

    // Connection will be deleted on complete transfer 
    // on indication by the http client code.

    // Get Response Header
    DSPublishResponse *resp = NULL;
    PublishResponseMap::iterator loc = publish_response_map_.find(serviceName);
    if (loc != publish_response_map_.end()) {
        resp = loc->second;
    } else {
        DISCOVERY_CLIENT_LOG_ERROR(DiscoveryClientLog, serviceName, 
                                   "Stray Publish Response");
        if (conn) {
            http_client_->RemoveConnection(conn);
        }
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::DOWN, ds_endpoint_,
            "Stray Publish Response");
        return;
    }

    if (xmls.empty()) {

        if (conn) {
            http_client_->RemoveConnection(conn);
        }

        // Errorcode is of type CURLcode
        if (ec.value() != 0) {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
                "PublishResponseHandler Error", serviceName, ec.value());
            // exponential back-off and retry
            resp->pub_fail_++;
            resp->attempts_++; 
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                ec.message());
            resp->StartPublishConnectTimer(resp->GetConnectTime());
        } else if (resp->publish_cb_called_ == false) {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
                "PublishResponseHandler, Only header received",
                 serviceName, ec.value());
            // exponential back-off and retry
            resp->pub_fail_++;
            resp->attempts_++; 
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                "Only header received");
            resp->StartPublishConnectTimer(resp->GetConnectTime());
        }

        return;
    }

    resp->publish_cb_called_ = true;
    //Parse the xml string and build DSResponse
    auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
    if (impl->LoadDoc(xmls) == -1) {
        DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
            "PublishResponseHandler: Loading Xml Doc failed!!",
            serviceName, ec.value());
        // exponential back-off and retry
        resp->pub_fail_++;
        resp->attempts_++; 
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::DOWN, ds_endpoint_,
            "Loading Xml Doc Failed");
        resp->StartPublishConnectTimer(resp->GetConnectTime());
        return;
    }

    DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, "PublishResponseHandler",
                           serviceName, xmls);
    //Extract cookie from message
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    pugi::xml_node node = pugi->FindNode("cookie");
    if (!pugi->IsNull(node)) {
        resp->cookie_ = node.child_value();
        resp->attempts_ = 0;
        resp->pub_rcvd_++;
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::UP, ds_endpoint_,
            "Publish Response - HeartBeat");
        //Start periodic heartbeat, timer per service 
        resp->StartHeartBeatTimer(5); // TODO hardcoded to 5secs
    } else {
        pugi::xml_node node = pugi->FindNode("h1");
        if (!pugi->IsNull(node)) {
            std::string response = node.child_value(); 
            if (response.compare("Error: 404 Not Found") == 0) {

                // Backward compatibility support, newer client and older
                // discovery server, fallback to older publish api
                DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, 
                    "PublishResponseHandler: 404 Not Found", serviceName,
                    "fallback to older publish api");
                resp->pub_fallback_++;
                resp->publish_hdr_.clear();
                resp->publish_hdr_ = "publish";
                Publish(serviceName); 
                return;
            } else {
                // 503, Service Unavailable
                // 504, Gateway Timeout, and other errors
                DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, 
                    "PublishResponseHandler: Error, resend publish",
                     serviceName, xmls);
            }
            // exponential back-off and retry
            resp->pub_fail_++;
            resp->attempts_++; 
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                response);
            resp->StartPublishConnectTimer(resp->GetConnectTime());
        } else {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg,
                "PublishResponseHandler: No [h1] tag, resend publish", 
                 serviceName, xmls);

            // exponential back-off and retry
            resp->pub_fail_++;
            resp->attempts_++; 
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                "No [h1] tag");
            resp->StartPublishConnectTimer(resp->GetConnectTime());
        }
    }
}

void DiscoveryServiceClient::Publish(std::string serviceName, std::string &msg) {
    //TODO save message for replay
    DSPublishResponse *pub_msg = new DSPublishResponse(serviceName, evm_, this);
    pub_msg->dss_ep_.address(ds_endpoint_.address());
    pub_msg->dss_ep_.port(ds_endpoint_.port());
    pub_msg->publish_msg_ = msg;
    boost::system::error_code ec;
    pub_msg->publish_hdr_ = "publish/" + boost::asio::ip::host_name(ec);
    pub_msg->pub_sent_++;

    //save it in a map
    publish_response_map_.insert(make_pair(serviceName, pub_msg)); 
     
    SendHttpPostMessage(pub_msg->publish_hdr_, serviceName, msg); 

    // Update connection info
    ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
        serviceName, ConnectionStatus::INIT, ds_endpoint_,
        "Publish");
    DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, pub_msg->publish_hdr_, 
                           serviceName, msg);
}

void DiscoveryServiceClient::Publish(std::string serviceName) {

    // Get Response Header
    PublishResponseMap::iterator loc = publish_response_map_.find(serviceName);
    if (loc != publish_response_map_.end()) {

        DSPublishResponse *resp = loc->second;
        resp->pub_sent_++; 
        SendHttpPostMessage(resp->publish_hdr_, serviceName, resp->publish_msg_);
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::INIT, ds_endpoint_,
            "Publish");

        DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, resp->publish_hdr_, 
                               serviceName, resp->publish_msg_);
    }
}

void DiscoveryServiceClient::WithdrawPublishInternal(std::string serviceName) {

    PublishResponseMap::iterator loc = publish_response_map_.find(serviceName);
    if (loc != publish_response_map_.end()) {
        DSPublishResponse *resp = loc->second;    

        resp->StopPublishConnectTimer();
        resp->StopHeartBeatTimer();
       
        publish_response_map_.erase(loc);
        delete resp;
    }
}

void DiscoveryServiceClient::WithdrawPublish(std::string serviceName) {
    assert(shutdown_ == false);
    work_queue_.Enqueue(boost::bind(&DiscoveryServiceClient::WithdrawPublishInternal,
                                    this, serviceName));
}

// Register application specific Response cb
void DiscoveryServiceClient::RegisterSubscribeResponseHandler(std::string serviceName,
                                                              ServiceHandler cb) {
    subscribe_map_.insert(make_pair(serviceName, cb));
}

void DiscoveryServiceClient::UnRegisterSubscribeResponseHandler(std::string serviceName) {
    subscribe_map_.erase(serviceName);
}

void DiscoveryServiceClient::Subscribe(std::string serviceName,
                                       uint8_t numbOfInstances, 
                                       ServiceHandler cb) {
    //Register the callback handler
    RegisterSubscribeResponseHandler(serviceName, cb); 

    //Build the DOM tree                                 
    auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
    impl->LoadDoc(""); 
    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    pugi->AddNode(serviceName, "");

    stringstream inst;
    inst << static_cast<int>(numbOfInstances);
    pugi->AddChildNode("instances", inst.str());
    pugi->ReadNode(serviceName); //Reset parent

    if (!subscriber_name_.empty()) {
        pugi->AddChildNode("client-type", subscriber_name_);
        pugi->ReadNode(serviceName); //Reset parent
    }
    boost::system::error_code error;
    string client_id = boost::asio::ip::host_name(error) + ":" + 
                       subscriber_name_;
    pugi->AddChildNode("client", client_id);
        
    stringstream ss; 
    impl->PrintDoc(ss);

    // Create Response Header
    ServiceResponseMap::iterator loc = service_response_map_.find(serviceName);
    if (loc == service_response_map_.end()) {
        DSResponseHeader *resp = new DSResponseHeader(serviceName, numbOfInstances,
                                                      evm_, this);
        //cache the request
        resp->subscribe_msg_ = ss.str();
        resp->sub_sent_++;
        service_response_map_.insert(make_pair(serviceName, resp));
    }

    SendHttpPostMessage("subscribe", serviceName, ss.str()); 
 
    // Update connection info
    ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
        serviceName, ConnectionStatus::INIT, ds_endpoint_,
        "Subscribe");
    DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, "subscribe",
                           serviceName, ss.str());
}

void DiscoveryServiceClient::Subscribe(std::string serviceName, uint8_t numbOfInstances) {
    // Get Response Header
    ServiceResponseMap::iterator loc = service_response_map_.find(serviceName);
    if (loc != service_response_map_.end()) {

        DSResponseHeader *resp = loc->second;
        resp->sub_sent_++;
        SendHttpPostMessage("subscribe", serviceName, resp->subscribe_msg_);
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::INIT, ds_endpoint_,
            "Subscribe");
    }
}

void DiscoveryServiceClient::UnsubscribeInternal(std::string serviceName) {

    ServiceResponseMap::iterator loc = service_response_map_.find(serviceName);
    if (loc != service_response_map_.end()) {
        DSResponseHeader *resp = loc->second;

        service_response_map_.erase(loc);
        delete resp;

        /* Remove registered subscribe callback handler */
        UnRegisterSubscribeResponseHandler(serviceName);
    }
}


void DiscoveryServiceClient::Unsubscribe(std::string serviceName) {
    assert(shutdown_ == false);
    work_queue_.Enqueue(boost::bind(&DiscoveryServiceClient::UnsubscribeInternal, 
                                    this, serviceName));
}

void DiscoveryServiceClient::SubscribeResponseHandler(std::string &xmls,
                                                      boost::system::error_code &ec,
                                                      std::string serviceName,
                                                      HttpConnection *conn)
{
    // Connection will be deleted on complete transfer 
    // on indication by the http client code.

    // Get Response Header
    DSResponseHeader *hdr = NULL; 
    ServiceResponseMap::iterator loc = service_response_map_.find(serviceName);
    if (loc != service_response_map_.end()) {
        hdr = loc->second;
    } else {
        DISCOVERY_CLIENT_LOG_ERROR(DiscoveryClientLog, serviceName, 
                                   "Stray Subscribe Response");
        if (conn) {
            http_client_->RemoveConnection(conn);
        }
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::DOWN, ds_endpoint_,
            "Stray Subscribe Response");
        return;
    }

    if (xmls.empty()) {
        if (conn) {
            http_client_->RemoveConnection(conn);
        }

        // Errorcode is of type CURLcode
        if (ec.value() != 0) {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
                "SubscribeResponseHandler Error",
                 serviceName, ec.value());
            // exponential back-off and retry
            hdr->attempts_++; 
            hdr->sub_fail_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_, ec.message());
            hdr->StartSubscribeTimer(hdr->GetConnectTime());
        } else  if (hdr->subscribe_cb_called_ == false) {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
                "SubscribeResponseHandler, Only header received",
                 serviceName, ec.value());
            // exponential back-off and retry
            hdr->attempts_++; 
            hdr->sub_fail_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                "Only header received");
            hdr->StartSubscribeTimer(hdr->GetConnectTime());
        }

        return;
    }

    hdr->subscribe_cb_called_= true;
    //Parse the xml string and build DSResponse
    auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
    if (impl->LoadDoc(xmls) == -1) {
        DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
            "SubscribeResponseHandler: Loading Xml Doc failed!!",
            serviceName, ec.value());
        // exponential back-off and retry
        hdr->attempts_++; 
        hdr->sub_fail_++;
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::DOWN, ds_endpoint_,
            "Loading Xml Doc Failed");
        hdr->StartSubscribeTimer(hdr->GetConnectTime());
        return;
    }

    //Extract ttl
    uint32_t ttl = 0;
    stringstream docs;

    XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());
    pugi::xml_node node_ttl = pugi->FindNode("ttl");
    if (!pugi->IsNull(node_ttl)) {
        string value(node_ttl.child_value());
        boost::trim(value);
        stringstream ss(value);
        ss >> ttl; 

        // Delete ttl for checksum calculation
        pugi->DeleteNode("ttl");
        impl->PrintDoc(docs);
    }

    pugi::xml_node node = pugi->FindNode("response");
    std::vector<DSResponse> ds_response;
    if (!pugi->IsNull(node)) {
        std::string serviceTag = node.first_child().name();
        for (node = node.first_child(); node; node = node.next_sibling()) {
       
            DSResponse resp;
            /* TODO: autogenerate with <choice> support */
            for (pugi::xml_node subnode = node.first_child(); subnode; 
                 subnode = subnode.next_sibling()) {
                string value(subnode.child_value());
                boost::trim(value);
                if (strcmp(subnode.name(), "ip-address") == 0) {
                    resp.ep.address(ip::address::from_string(value));
                } else  if (strcmp(subnode.name(), "port") == 0) {
                    uint32_t port; 
                    stringstream sport(value);
                    sport >> port; 
                    resp.ep.port(port);
                }
            } 
            ds_response.push_back(resp);
        }


        // generate hash of the message
        boost::hash<std::string> string_hash;
        uint32_t gen_chksum = string_hash(docs.str());

        hdr->sub_rcvd_++;
        hdr->attempts_ = 0;
        if ((hdr->chksum_ == gen_chksum) || (ds_response.size() == 0)) {
            //Restart Subscribe Timer
            hdr->StartSubscribeTimer(ttl);
            return; //No change in message, ignore
        }

        DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, "SubscribeResponseHandler",
                               serviceName, xmls);
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::UP, ds_endpoint_,
                "SubscribeResponse");

        // Update DSResponseHeader for first response or change in response
        hdr->chksum_ = gen_chksum;

        hdr->service_list_.clear();
        hdr->service_list_ = ds_response;

        //Start Subscribe Timer
        hdr->StartSubscribeTimer(ttl);

        // Call the Registered Handler building DSResponse from xml-string
        SubscribeResponseHandlerMap::iterator it = subscribe_map_.find(serviceName);
        if (it != subscribe_map_.end()) {
            ServiceHandler cb = it->second;
            cb(ds_response); 
        } 
    } else {
        pugi::xml_node node = pugi->FindNode("h1");
        if (!pugi->IsNull(node)) {
            std::string response = node.child_value(); 
            // 503, Service Unavailable
            // 504, Gateway Timeout, and other errors
            DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, 
                "SubscribeResponseHandler: Error, resend subscribe",
                serviceName, xmls);

            // exponential back-off and retry
            hdr->attempts_++; 
            hdr->sub_fail_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_, response);
            hdr->StartSubscribeTimer(hdr->GetConnectTime());

        } else {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg,
                "SubscribeResponseHandler: No [h1] tag, resend subscribe", 
                 serviceName, xmls);

            // exponential back-off and retry
            hdr->attempts_++; 
            hdr->sub_fail_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                "No [h1] tag");
            //Use connect timer as subscribe timer 
            hdr->StartSubscribeTimer(hdr->GetConnectTime());
        }
    }
}

void DiscoveryServiceClient::SendHeartBeat(std::string serviceName,
                                    std::string msg) {
    DSPublishResponse *resp = GetPublishResponse(serviceName); 
    resp->pub_hb_sent_++;
    SendHttpPostMessage("heartbeat", serviceName, msg);
}

void DiscoveryServiceClient::HeartBeatResponseHandler(std::string &xmls, 
                                                      boost::system::error_code ec, 
                                                      std::string serviceName,
                                                      HttpConnection *conn) {

    // Get Response Header
    DSPublishResponse *resp = NULL;
    PublishResponseMap::iterator loc = publish_response_map_.find(serviceName);
    if (loc != publish_response_map_.end()) {
        resp = loc->second;
    } else {
        DISCOVERY_CLIENT_LOG_ERROR(DiscoveryClientLog, serviceName, 
                                   "Stray HeartBeat Response");
        if (conn) {
            http_client_->RemoveConnection(conn);
        }
        // Update connection info
        ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
            serviceName, ConnectionStatus::DOWN, ds_endpoint_,
            "Stray HeartBeat Response");
        return;
    }

    // Connection will be deleted on complete transfer 
    // on indication by the http client code.
    if (xmls.empty()) {

        if (conn) {
            http_client_->RemoveConnection(conn);
        }

        // Errorcode is of type CURLcode
        if (ec.value() != 0) {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
                "HeartBeatResponseHandler Error", serviceName, ec.value());
            resp->pub_hb_fail_++;
            resp->StopHeartBeatTimer();
            // Resend original publish request after exponential back-off
            resp->attempts_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                ec.message());
             resp->StartPublishConnectTimer(resp->GetConnectTime());
        } else if (resp->heartbeat_cb_called_ == false) {
            DISCOVERY_CLIENT_TRACE(DiscoveryClientErrorMsg,
                "HeartBeatResponseHandler, Only header received",
                 serviceName, ec.value());
            resp->pub_hb_fail_++;
            resp->StopHeartBeatTimer();
            // Resend original publish request after exponential back-off
            resp->attempts_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                "Only header received");
             resp->StartPublishConnectTimer(resp->GetConnectTime());
        }

        return;
    }

    resp->heartbeat_cb_called_ = true;
    if (xmls.find("200 OK") == std::string::npos) {
        DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, 
            "HeartBeatResponseHandler Not OK, resend publish",
            serviceName, xmls);
            //response is not OK, resend publish
            resp->pub_hb_fail_++;
            resp->StopHeartBeatTimer(); 
            resp->attempts_++;
            // Update connection info
            ConnectionState::GetInstance()->Update(ConnectionType::DISCOVERY,
                serviceName, ConnectionStatus::DOWN, ds_endpoint_,
                "HeartBeatResponse NOT 200 OK");
            resp->StartPublishConnectTimer(resp->GetConnectTime());
            return;
    }
    resp->pub_hb_rcvd_++;
}

void DiscoveryServiceClient::SendHttpPostMessage(std::string msg_type, 
                                                 std::string serviceName,
                                                 std::string msg) {

    HttpConnection *conn = http_client_->CreateConnection(ds_endpoint_);
    if (msg_type.compare("subscribe") == 0) {
        conn->HttpPost(msg, msg_type,
            boost::bind(&DiscoveryServiceClient::SubscribeResponseHandler,
                        this, _1, _2, serviceName, conn));
    } else if (msg_type.find("publish") != string::npos) {
        conn->HttpPost(msg, msg_type,
            boost::bind(&DiscoveryServiceClient::PublishResponseHandler,
                        this, _1, _2, serviceName, conn));
    } else if (msg_type.find("heartbeat") != string::npos) {
        conn->HttpPost(msg, msg_type,
            boost::bind(&DiscoveryServiceClient::HeartBeatResponseHandler,
                        this, _1, _2, serviceName, conn));
    } else {
        DISCOVERY_CLIENT_TRACE(DiscoveryClientMsg, 
                               msg_type, serviceName, 
                               "Invalid message type");
    }

}

DSPublishResponse *DiscoveryServiceClient::GetPublishResponse(
                                             std::string serviceName) {

    DSPublishResponse *resp = NULL;
    PublishResponseMap::iterator loc = publish_response_map_.find(serviceName);
    if (loc != publish_response_map_.end()) {
        resp = loc->second;
    }

    return resp;
}

void DiscoveryServiceClient::FillDiscoveryServiceSubscriberStats(
         std::vector<DiscoveryClientSubscriberStats> &ds_stats) {

    for (DiscoveryServiceClient::ServiceResponseMap::const_iterator iter =
         service_response_map_.begin(),
         next = iter;
         iter != service_response_map_.end(); iter = next)  {  

         DSResponseHeader *sub_resp = iter->second; 

         DiscoveryClientSubscriberStats stats; 
         stats.set_serviceName(sub_resp->serviceName_); 
         stats.set_subscribe_sent(sub_resp->sub_sent_); 
         stats.set_subscribe_rcvd(sub_resp->sub_rcvd_);
         stats.set_subscribe_fail(sub_resp->sub_fail_);
         stats.set_subscribe_retries(sub_resp->attempts_); 

         ds_stats.push_back(stats);

         next++;
     }
}


void DiscoveryServiceClient::FillDiscoveryServicePublisherStats(
         std::vector<DiscoveryClientPublisherStats> &ds_stats) {

    for (DiscoveryServiceClient::PublishResponseMap::const_iterator iter =  
         publish_response_map_.begin(), 
         next = iter;
         iter != publish_response_map_.end(); iter = next)  { 

         DSPublishResponse *pub_resp = iter->second;

         DiscoveryClientPublisherStats stats;
         stats.set_serviceName(pub_resp->serviceName_); 
         stats.set_heartbeat_sent(pub_resp->pub_hb_sent_);
         stats.set_heartbeat_fail(pub_resp->pub_hb_fail_);   
         stats.set_heartbeat_rcvd(pub_resp->pub_hb_rcvd_);
         stats.set_publish_sent(pub_resp->pub_sent_);
         stats.set_publish_rcvd(pub_resp->pub_rcvd_); 
         stats.set_publish_fail(pub_resp->pub_fail_); 
         stats.set_publish_retries(pub_resp->attempts_);
         stats.set_publish_fallback(pub_resp->pub_fallback_); 

         ds_stats.push_back(stats);

         next++;
    } 
}
