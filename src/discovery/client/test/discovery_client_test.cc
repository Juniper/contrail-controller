/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "../discovery_client.h"

#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "base/logging.h"
#include "base/util.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>

#include "xml/xml_base.h"
#include "xml/xml_pugi.h"

#include "testing/gunit.h"

using namespace std;
using namespace pugi;
namespace ip = boost::asio::ip;

class DiscoveryServiceClientMock: public DiscoveryServiceClient {
public:
    DiscoveryServiceClientMock(EventManager *evm, boost::asio::ip::tcp::endpoint ep) :
        DiscoveryServiceClient(evm, ep) , xmpp_instances_(0), ifmap_instances_(0),
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_publish->Init();
 
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::IFMapService, msg);

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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_publish->Init();
 
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::IFMapService, msg);

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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_subscribe->Init();

    //subscribe to service
    dsc_subscribe->Subscribe("Test", DiscoveryServiceClient::XmppService, 1, 
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_publish->Init();
 
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::XmppService, msg);


    //Subscribe DS client
    DiscoveryServiceClientMock *dsc_subscribe = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_subscribe->Init();

    //subscribe to service
    dsc_subscribe->Subscribe("Test", DiscoveryServiceClient::XmppService, 1, 
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_pubsub->Init();
 
    //Publish xmpp-server service
    std::string msg;
    dsc_pubsub->BuildPublishMessage("xmpp-server", msg); 
    dsc_pubsub->Publish(DiscoveryServiceClient::XmppService, msg);

    //subscribe to service
    dsc_pubsub->Subscribe("Test", DiscoveryServiceClient::CollectorService, 1, 
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc->Init();
 
    int ifmap_instances = 1;
    //subscribe to service
    dsc->Subscribe("Test", DiscoveryServiceClient::IFMapService, ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 

    //subscribe response 
    std::string msg;
    dsc->BuildServiceResponseMessage("ifmap-server", ifmap_instances, msg); 
    boost::system::error_code ec; 
    dsc->SubscribeResponseHandler(msg, ec, DiscoveryServiceClient::IFMapService, NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 1);
    EXPECT_TRUE(dsc->IfMapInstances() == ifmap_instances);
    task_util::WaitForIdle(); 

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe(DiscoveryServiceClient::IFMapService); 
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_publish->Init();
 
    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::IFMapService, msg);

    // send publisher cookie response
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("ifmap-server", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, DiscoveryServiceClient::IFMapService, NULL);
    task_util::WaitForIdle(); 
    
    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish(DiscoveryServiceClient::IFMapService);
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc->Init();
 
    int xmpp_instances = 2;
    int ifmap_instances = 1;

    //subscribe to service
    dsc->Subscribe("Test", DiscoveryServiceClient::IFMapService, ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 
    dsc->Subscribe("Test", DiscoveryServiceClient::XmppService, xmpp_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeXmppHandler, dsc, _1));

    std::string msg;
    boost::system::error_code ec; 
    dsc->BuildServiceResponseMessage("ifmap-server", ifmap_instances, msg); 
    dsc->SubscribeResponseHandler(msg, ec, DiscoveryServiceClient::IFMapService, NULL);
    EXPECT_TRUE(dsc->IfMapInstances() == ifmap_instances);

    dsc->BuildServiceResponseMessage("xmpp-server", xmpp_instances, msg); 
    dsc->SubscribeResponseHandler(msg, ec, DiscoveryServiceClient::XmppService, NULL);
    EXPECT_TRUE(dsc->XmppInstances() == xmpp_instances);

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe(DiscoveryServiceClient::IFMapService); 
    dsc->Unsubscribe(DiscoveryServiceClient::XmppService); 
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_publish->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("ifmap-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::IFMapService, msg);

    dsc_publish->BuildPublishMessage("xmpp-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::XmppService, msg);

    // send publisher cookie response
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("ifmap-server", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, DiscoveryServiceClient::IFMapService, NULL);

    dsc_publish->BuildPublishResponseMessage("xmpp-server", msg2);
    dsc_publish->PublishResponseHandler(msg2, ec, DiscoveryServiceClient::XmppService, NULL);
    
    EvmShutdown();

    //withdraw publish service
    dsc_publish->WithdrawPublish(DiscoveryServiceClient::IFMapService);
    dsc_publish->WithdrawPublish(DiscoveryServiceClient::XmppService);
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_publish->Init();

    DiscoveryServiceClientMock *dsc_subscribe = 
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc_subscribe->Init();

    //publish a service
    std::string msg;
    dsc_publish->BuildPublishMessage("xmpp-server", msg); 
    dsc_publish->Publish(DiscoveryServiceClient::XmppService, msg);

    // send publisher cookie response
    std::string msg2;
    dsc_publish->BuildPublishResponseMessage("xmpp-server", msg2);
    boost::system::error_code ec; 
    dsc_publish->PublishResponseHandler(msg2, ec, DiscoveryServiceClient::XmppService, NULL);


    //subscribe a service
    dsc_subscribe->Subscribe("Test", DiscoveryServiceClient::XmppService, 1, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeXmppHandler, dsc_subscribe, _1));
    task_util::WaitForIdle(); 

    //subscribe response 
    dsc_subscribe->BuildServiceResponseMessage("xmpp-server", 1, msg); 
    dsc_subscribe->SubscribeResponseHandler(msg, ec, DiscoveryServiceClient::XmppService, NULL);
    EXPECT_TRUE(dsc_subscribe->XmppInstances() == 1);

    EvmShutdown();

    //unsubscribe to service
    dsc_subscribe->Unsubscribe(DiscoveryServiceClient::XmppService); 

    //withdraw publish service
    dsc_publish->WithdrawPublish(DiscoveryServiceClient::XmppService);
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
        (new DiscoveryServiceClientMock(evm_.get(), dss_ep));
    dsc->Init();
 
    int ifmap_instances = 1;
    //subscribe to service
    dsc->Subscribe("Test", DiscoveryServiceClient::IFMapService, ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 

    //subscribe response with no publisher
    std::string msg;
    dsc->BuildServiceResponseMessage("ifmap-server", 0, msg); 
    boost::system::error_code ec; 
    dsc->SubscribeResponseHandler(msg, ec, DiscoveryServiceClient::IFMapService, NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 0);
    task_util::WaitForIdle(); 

    //Resubscribe assume ttl/connect-time expired
    dsc->Subscribe("Test", DiscoveryServiceClient::IFMapService, ifmap_instances, 
        boost::bind(&DiscoveryServiceClientMock::AsyncSubscribeIfMapHandler, dsc, _1));
    task_util::WaitForIdle(); 
   
    //subscribe response with no publisher
    dsc->BuildServiceResponseMessage("ifmap-server", ifmap_instances, msg); 
    dsc->SubscribeResponseHandler(msg, ec, DiscoveryServiceClient::IFMapService, NULL);
    EXPECT_TRUE(dsc->IfMapCbCount() == 1);
    EXPECT_TRUE(dsc->IfMapInstances() == ifmap_instances);
    task_util::WaitForIdle(); 

    EvmShutdown();

    //unsubscribe to service
    dsc->Unsubscribe(DiscoveryServiceClient::IFMapService); 
    task_util::WaitForIdle(); 

    dsc->Shutdown(); // No more listening socket, clear sessions
    task_util::WaitForIdle(); 
    delete dsc;
    task_util::WaitForIdle(); 
}


}

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
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
