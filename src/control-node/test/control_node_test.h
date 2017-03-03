/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__control_node_test__
#define __ctrlplane__control_node_test__

#include <boost/scoped_ptr.hpp>
#include <vector>
#include "ifmap/ifmap_config_options.h"

class BgpServer;
class BgpServerTest;
class DB;
class ConfigClientManager;
class EventManager;
class IFMapChannelManager;
class IFMapServer;
class XmppServer;
class XmppChannelConfig;
class BgpXmppChannelManager;

namespace test {


class ControlNodeTest {
public:
    static const char *kNodeJID;
    ControlNodeTest(EventManager *evm, const std::string &hostname);
    ~ControlNodeTest();

    void BgpConfig(const std::string &config);
    void IFMapMessage(const std::string &msg);
    void VerifyRoutingInstance(const std::string instance,
                               bool verify_network_index = true);

    int BgpEstablishedCount() const;

    const std::string &localname() const;

    int bgp_port() const;
    int xmpp_port() const;

    DB *config_db();

    BgpServerTest *bgp_server();
    XmppServer *xmpp_server();
    BgpXmppChannelManager *xmpp_channel_manager();
    void Shutdown();

private:
    XmppChannelConfig *CreateChannelConfig() const;

    static void SetUp();
    static void TearDown();

    static int node_count_;
    boost::scoped_ptr<BgpServerTest> bgp_server_;
    XmppServer *xmpp_server_;
    boost::scoped_ptr<IFMapServer> map_server_;
    boost::scoped_ptr<BgpXmppChannelManager> xmpp_manager_;
    boost::scoped_ptr<IFMapChannelManager> map_manager_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
};

}  // test

#endif /* defined(__ctrlplane__control_node_test__) */
