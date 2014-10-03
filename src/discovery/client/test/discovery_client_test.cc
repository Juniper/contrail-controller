/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>

#include "base/logging.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

#include "xml/xml_base.h"
#include "xml/xml_pugi.h"

#include "../discovery_client.h"
#include "testing/gunit.h"

using namespace std;
using namespace pugi;
namespace ip = boost::asio::ip;

class DiscoveryServiceClientMock: public DiscoveryServiceClient {
public:
    DiscoveryServiceClientMock(EventManager *evm, boost::asio::ip::tcp::endpoint ep,
                               std::string client_name) :
        DiscoveryServiceClient(evm, ep, client_name) , 
        xmpp_instances_(0), ifmap_instances_(0),
        xmpp_cb_count_(0), ifmap_cb_count_(0) {
    }

    void AsyncSubscribeXmppHandler(std::vector<DSResponse> dr) {
        //Connect handler for the service
        xmpp_cb_count_++; 
        xmpp_instances_ = dr.size();
    }

    void AsyncSubscribeIfMapHandler(std::vector<DSResponse> dr) {
        //Connect handler for the service
        ifmap_cb_count_++; 
        ifmap_instances_ = dr.size();
    }

    void AsyncCollectorHandler(std::vector<DSResponse> dr) {
    }

    void BuildServiceResponseMessage(std::string serviceNameTag, uint num_instances,
                                       std::string &msg) {
        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        impl->LoadDoc("");
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

        pugi->AddChildNode("response", "");
        if (num_instances) {
            pugi->AddChildNode(serviceNameTag.c_str(), "");
            while (num_instances--) {
                pugi->AddChildNode("ip-address", "127.0.0.1");
                pugi->ReadNode(serviceNameTag.c_str());
                pugi->AddChildNode("port", "5555");
                pugi->ReadNode("response");
            }
        }
        pugi->ReadNode("response");
        pugi->AddChildNode("ttl", "10"); //ttl is per subscribe in seconds

        stringstream ss;
        impl->PrintDoc(ss);
        msg = ss.str(); 
    }

    void BuildHeartBeatResponseMessage(std::string serviceNameTag, 
                                       std::string &msg) {
        stringstream ss;
        ss << "200 OK";
        msg = ss.str(); 
    }

    /* 503, Service Unavailable error is seen when proxy is not
     * able to contact the Discovery Server, the error is in the 
     * body of the message */
    void BuildHeartBeat503ResponseMessage(std::string serviceNameTag,
                                       std::string &msg) {

        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        impl->LoadDoc("");
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

        pugi->AddChildNode("html", "");
        pugi->AddChildNode("body", "");
        pugi->AddChildNode("h1", "503 Service Unavailable");

        stringstream ss;
        impl->PrintDoc(ss);
        msg = ss.str(); 
    }

    void BuildNullServiceResponseMessage(std::string serviceNameTag, 
                                         std::string &msg) {

        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        impl->LoadDoc("");
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

        pugi->AddChildNode("bad-response", "");

        stringstream ss;
        impl->PrintDoc(ss);
        msg = ss.str(); 
    }

    void BuildPublishMessage(std::string serviceNameTag, std::string &msg) {
        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        impl->LoadDoc("");
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

        pugi->AddChildNode(serviceNameTag.c_str(), "");
        pugi->AddChildNode("ip-address", "127.0.0.1");
        pugi->ReadNode(serviceNameTag.c_str());
        pugi->AddChildNode("port", "5555");

        stringstream ss;
        impl->PrintDoc(ss);
        msg = ss.str(); 
    }

    void BuildPublishResponseMessage(std::string serviceNameTag, std::string &msg) {
        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        impl->LoadDoc("");
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

        pugi->AddChildNode("response", "");
        pugi->AddChildNode(serviceNameTag.c_str(), "");
        pugi->AddChildNode("cookie", "952012c31dd56951b9177930af75c73e");

        stringstream ss;
        impl->PrintDoc(ss);
        msg = ss.str(); 
    }

    void BuildPublishResponseMessageNocookie(std::string serviceNameTag, 
                                             std::string &msg) {
        auto_ptr<XmlBase> impl(XmppXmlImplFactory::Instance()->GetXmlImpl());
        impl->LoadDoc("");
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl.get());

        pugi->AddChildNode("h1", "503 Service Unavailable");

        stringstream ss;
        impl->PrintDoc(ss);
        msg = ss.str(); 
    }

    int XmppInstances() { return xmpp_instances_; }
    int IfMapInstances() { return ifmap_instances_; }
    int XmppCbCount() { return xmpp_cb_count_; }
    int IfMapCbCount() { return ifmap_cb_count_; }

private:
    uint8_t xmpp_instances_;
    uint8_t ifmap_instances_;
    uint8_t xmpp_cb_count_;
    uint8_t ifmap_cb_count_;
};

class DiscoveryServiceClientTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        thread_.reset(new ServerThread(evm_.get())); 
        thread_->Start();
    }

    void EvmShutdown() {
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle(); 
    }
    virtual void TearDown() {
        task_util::WaitForIdle(); 
    }

    auto_ptr<EventManager> evm_; 
    auto_ptr<ServerThread> thread_;
};

namespace {

// Test with real DSS daemon
// Send publish message and delete DS client before callback
// http client library to cleanup the http client connection
// and session
#if 0
TEST_F(DiscoveryServiceClientTest, DSS_no_publish_cb) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("10.84.7.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc_publish->Init();
 
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server-test", msg); 
    dsc_publish->Publish("ifmap-server-test", msg);

    while(true) {
        sleep(10);
    }

    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}


// Test with real DSS daemon
// Send publish message and wait for publish callback
// which deletes the http client connection and session
TEST_F(DiscoveryServiceClientTest, DSS_with_publish_cb) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("10.84.7.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc_publish->Init();
 
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server-test", msg); 
    dsc_publish->Publish("ifmap-server-test", msg);

    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);

    //Wait for connection to be closed
    task_util::WaitForIdle(); 

    //Test connection failures, publisher heart-beat
    while (true) {
        sleep(10);
    }

    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}

// Test with real DSS daemon
// Send subscribe message and wait for subscribe callback
// which deletes the http client connection and session
TEST_F(DiscoveryServiceClientTest, DSS_with_subscribe_cb) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("10.84.7.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_subscribe = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc_subscribe->Init();

    //subscribe to service
    dsc_subscribe->Subscribe("xmpp-server-test", 1, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeXmppHandler, 
                    dsc_subscribe, _1));


    // Test ttl expiry/subscribe refresh
    // and connection failures
    while (true) {
        sleep(10);
    }

    //Wait for connection to be closed
    task_util::WaitForIdle(); 
    dsc_subscribe->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_subscribe;
    task_util::WaitForIdle(); 
}


// Test with real DSS daemon
// Send client 1, publish message and wait for publish callback
// Send client 2, subscribe message and wait for publish callback
TEST_F(DiscoveryServiceClientTest, DSS_pubsub_clients) {

    //Publish DS client
    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("10.84.7.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc_publish->Init();
 
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);


    //Subscribe DS client
    DiscoveryServiceClientMock *dsc_subscribe = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test2"));
    dsc_subscribe->Init();

    //subscribe to service
    dsc_subscribe->Subscribe("xmpp-server-test", 1, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeXmppHandler, 
                    dsc_subscribe, _1));

    // check for ttl expiry and subscribe refresh
    while (true) {
        sleep(10);
    }

    task_util::WaitForIdle(); 
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 

    task_util::WaitForIdle(); 
    dsc_subscribe->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_subscribe;
    task_util::WaitForIdle(); 
}

// Test with real DSS daemon
// Send client 1, publish message and wait for publish callback
// Send client 1, subscribe message and wait for publish callback
TEST_F(DiscoveryServiceClientTest, DSS_pubsub_client) {

    //Publish DS client
    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("10.84.7.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_pubsub = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc_pubsub->Init();
 
    //Publish xmpp-server service
    std::string msg;
    dsc_pubsub->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_pubsub->Publish("xmpp-server-test", msg);

    //subscribe to service
    dsc_pubsub->Subscribe("collector-server-test", 1, 
        boost::bind(&DiscoveryServiceClientMock::AsyncCollectorHandler, 
                    dsc_pubsub, _1));

    // check for ttl expiry and subscribe refresh
    while (true) {
        sleep(10);
    }

    task_util::WaitForIdle(); 
    dsc_pubsub->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_pubsub;
    task_util::WaitForIdle(); 

}
#endif

TEST_F(DiscoveryServiceClientTest, Subscribe_1_Service) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5997);
    DiscoveryServiceClientMock *dsc = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc->Init();
 
    int ifmap_instances = 1;
    //subscribe to service
    dsc->Subscribe("ifmap-server-test", ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 

    //subscribe response 
    std::string msg;
    dsc->BuildServiceResponseMessage("ifmap-server-test", ifmap_instances, msg); 
    boost::system::error_code ec; 
    dsc->SubscribeResponseHandler(msg, ec, "ifmap-server-test", NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 1);
    EXPECT_TRUE(dsc->IfMapInstances() == ifmap_instances);
    task_util::WaitForIdle(); 

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe("ifmap-server-test"); 
    task_util::WaitForIdle(); 

    dsc->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, Publish_1_Service) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5997);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test")); 
    dsc_publish->Init();
 
    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server-test", msg); 
    dsc_publish->Publish("ifmap-server-test", msg);

    // send publisher cookie response
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("ifmap-server-test", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, "ifmap-server-test", NULL);
    task_util::WaitForIdle(); 
    
    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish("ifmap-server-test");
    task_util::WaitForIdle(); 
    
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, Subscribe_Services) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test"));
    dsc->Init();
 
    int xmpp_instances = 2;
    int ifmap_instances = 1;

    //subscribe to service
    dsc->Subscribe("ifmap-server-test", ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 
    dsc->Subscribe("xmpp-server-test", xmpp_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeXmppHandler, dsc, _1));

    std::string msg;
    boost::system::error_code ec; 
    dsc->BuildServiceResponseMessage("ifmap-server-test", ifmap_instances, msg); 
    dsc->SubscribeResponseHandler(msg, ec, "ifmap-server-test", NULL);
    EXPECT_TRUE(dsc->IfMapInstances() == ifmap_instances);

    dsc->BuildServiceResponseMessage("xmpp-server-test", xmpp_instances, msg); 
    dsc->SubscribeResponseHandler(msg, ec, "xmpp-server-test", NULL);
    EXPECT_TRUE(dsc->XmppInstances() == xmpp_instances);

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe("ifmap-server-test"); 
    dsc->Unsubscribe("xmpp-server-test"); 
    task_util::WaitForIdle(); 

    dsc->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, Publish_Services) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test")); 
    dsc_publish->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server-test", msg); 
    dsc_publish->Publish("ifmap-server-test", msg);

    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);

    // send publisher cookie response
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("ifmap-server-test", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, "ifmap-server-test", NULL);

    dsc_publish->BuildPublishResponseMessage("xmpp-server-test", msg2);
    dsc_publish->PublishResponseHandler(msg2, ec, "xmpp-server-test", NULL);
    
    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish("ifmap-server-test");
    dsc_publish->WithdrawPublish("xmpp-server-test");
    task_util::WaitForIdle(); 
    
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, Publish_Subscribe_1_Service) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test1"));
    dsc_publish->Init();

    DiscoveryServiceClientMock *dsc_subscribe = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test2"));
    dsc_subscribe->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);

    // send publisher cookie response
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("xmpp-server-test", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, "xmpp-server-test", NULL);


    //subscribe a service
    dsc_subscribe->Subscribe("xmpp-server-test", 1, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeXmppHandler, dsc_subscribe, _1));
    task_util::WaitForIdle(); 

    //subscribe response 
    dsc_subscribe->BuildServiceResponseMessage("xmpp-server-test", 1, msg); 
    dsc_subscribe->SubscribeResponseHandler(msg, ec, "xmpp-server-test", NULL);
    EXPECT_TRUE(dsc_subscribe->XmppInstances() == 1);

    EvmShutdown();

    //unsubscribe to service
    dsc_subscribe->Unsubscribe("xmpp-server-test"); 

    //withdraw publish service
    dsc_publish->WithdrawPublish("xmpp-server-test");
    task_util::WaitForIdle(); 
    
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 

    dsc_subscribe->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_subscribe;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, Subscribe_1_Service_nopublisher) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5997);
    DiscoveryServiceClientMock *dsc = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc->Init();
 
    int ifmap_instances = 1;
    //subscribe to service
    dsc->Subscribe("ifmap-server-test", ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 

    //subscribe response with no publisher
    std::string msg;
    dsc->BuildServiceResponseMessage("ifmap-server-test", 0, msg); 
    boost::system::error_code ec; 
    dsc->SubscribeResponseHandler(msg, ec, "ifmap-server-test", NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 0);
    task_util::WaitForIdle(); 

    //Resubscribe assume ttl/connect-time expired
    dsc->Subscribe("ifmap-server-test", ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 
   
    //subscribe response with no publisher
    dsc->BuildServiceResponseMessage("ifmap-server-test", ifmap_instances, msg); 
    dsc->SubscribeResponseHandler(msg, ec, "ifmap-server-test", NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 1);
    EXPECT_TRUE(dsc->IfMapInstances() == ifmap_instances);
    task_util::WaitForIdle(); 

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe("ifmap-server-test"); 
    task_util::WaitForIdle(); 

    dsc->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc;
    task_util::WaitForIdle(); 
}


TEST_F(DiscoveryServiceClientTest, Subscribe_1_Service_badresponse) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5997);
    DiscoveryServiceClientMock *dsc = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep,
         "DS-Test"));
    dsc->Init();
 
    int ifmap_instances = 1;
    //subscribe to service
    dsc->Subscribe("ifmap-server-test", ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 

    //subscribe response with no publisher
    std::string msg;
    dsc->BuildNullServiceResponseMessage("ifmap-server-test", msg);
    boost::system::error_code ec; 
    dsc->SubscribeResponseHandler(msg, ec, "ifmap-server-test", NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 0);
    task_util::WaitForIdle(); 

    //Ensure we do send resubscribes.

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe("ifmap-server-test"); 
    task_util::WaitForIdle(); 

    dsc->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, Publish_No_Cookie_Response) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test1"));
    dsc_publish->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);

    // send publish response with no cookie
    std::string msg2;
    dsc_publish->BuildPublishResponseMessageNocookie("xmpp-server-test", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, "xmpp-server-test", NULL);

    DSPublishResponse *resp = dsc_publish->GetPublishResponse("xmpp-server-test");
    if (resp) {
        EXPECT_TRUE(resp->pub_fail_ >= 1);
    }

    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish("xmpp-server-test");
    task_util::WaitForIdle(); 
    
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, HeartBeat) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test1"));
    dsc_publish->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);

    // send publish response 
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("xmpp-server-test", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, "xmpp-server-test", NULL);

    // send heart-beat response
    std::string msg3;
    dsc_publish->BuildHeartBeatResponseMessage("xmpp-server-test", msg3);
    dsc_publish->HeartBeatResponseHandler(msg3, ec, "xmpp-server-test", NULL);

    // ensure heart-beat parsed
    DSPublishResponse *resp = dsc_publish->GetPublishResponse("xmpp-server-test");
    if (resp) {
        EXPECT_TRUE(resp->pub_hb_rcvd_ == 1);
    }

    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish("xmpp-server-test");
    task_util::WaitForIdle(); 
    
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}

TEST_F(DiscoveryServiceClientTest, HeartBeat_503_Response) {

    ip::tcp::endpoint dss_ep;
    dss_ep.address(ip::address::from_string("127.0.0.1"));
    dss_ep.port(5998);
    DiscoveryServiceClientMock *dsc_publish = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep, "DS-Test1"));
    dsc_publish->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server-test", msg); 
    dsc_publish->Publish("xmpp-server-test", msg);

    // send publish response 
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("xmpp-server-test", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, "xmpp-server-test", NULL);

    // send heart-beat response
    std::string msg3;
    dsc_publish->BuildHeartBeat503ResponseMessage("xmpp-server-test", msg3);
    dsc_publish->HeartBeatResponseHandler(msg3, ec, "xmpp-server-test", NULL);

    // ensure heart-beat parsed
    DSPublishResponse *resp = dsc_publish->GetPublishResponse("xmpp-server-test");
    if (resp) {
        EXPECT_TRUE(resp->pub_hb_fail_ == 1);
    }

    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish("xmpp-server-test");
    task_util::WaitForIdle(); 
    
    dsc_publish->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc_publish;
    task_util::WaitForIdle(); 
}


}

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown(); 
    return result;
}
