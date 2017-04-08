/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __BGP__BGP_STRESS_TEST_H__
#define __BGP__BGP_STRESS_TEST_H__

#include <boost/uuid/name_generator.hpp>
#include "control-node/test/network_agent_mock.h"
#include "database/gendb_if.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/inet/inet_route.h"
#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_cassandra_client.h"
#include "ifmap/client/config_client_manager.h"
#include "ifmap/client/config_db_client.h"
#include "ifmap/client/config_json_parser.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_server.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"
#include "testing/gunit.h"

class BgpAttr;
class BgpNeighborConfig;
class BgpServer;
class ConfigClientManager;
class SandeshSession;
class StateMachine;
class XmppChannel;
class XmppChannelConfig;
class XmppConnection;

class ConfigCassandraClientTest : public ConfigCassandraClient {
public:
    ConfigCassandraClientTest(ConfigClientManager *mgr, EventManager *evm,
        const IFMapConfigOptions &options, ConfigJsonParser *in_parser,
        int num_workers) : ConfigCassandraClient(mgr, evm, options, in_parser,
            num_workers) {
    }

    virtual int HashUUID(const string &uuid_str) const {
        return ConfigCassandraClient::HashUUID(uuid_str);
    }

    virtual void HandleObjectDelete(const std::string &uuid) {
        ConfigCassandraClient::HandleObjectDelete(uuid);
    }

    virtual bool ProcessObjUUIDTableEntry(const string &uuid_key,
                                          const GenDb::ColList &col_list) {
        return ConfigCassandraClient::ProcessObjUUIDTableEntry(uuid_key,
                                                               col_list);
    }
};

class IFMapServerTest : public IFMapServer {
public:
    IFMapServerTest(DB *db, DBGraph *graph, boost::asio::io_service *io_service)
        : IFMapServer(db, graph, io_service), config_client_manager_(NULL) {
    }

    virtual ~IFMapServerTest() { }

    void set_config_client_manager(ConfigClientManager *config_client_manager) {
        config_client_manager_ = config_client_manager;
    }

    ConfigCassandraClientTest *cassandra_client() {
        if (!config_client_manager_)
            return NULL;
        return dynamic_cast<ConfigCassandraClientTest *>(
                config_client_manager_->config_db_client());
    }

private:
    ConfigClientManager *config_client_manager_;
};

class IFMapXmppChannelTest : public IFMapXmppChannel {
public:
    IFMapXmppChannelTest(XmppChannel *channel, IFMapServer *server,
                         IFMapChannelManager *manager) :
            IFMapXmppChannel(channel, server, manager),
            ifmap_server_(server) {
    }

    virtual ~IFMapXmppChannelTest() { }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        // Inject virtual-machine and virtual-router configurations so that
        // they get downloaded to agents later on.
        CreatefmapConfig(msg);
        IFMapXmppChannel::ReceiveUpdate(msg);
    }

    // Split uuid hex string (with -'s) into upper and lower 8 bytes decimal
    // digitis string.
    void UuidToDecimal(std::string uuid, std::string &uuid_msb,
                       std::string &uuid_lsb) {

        // Remove all '-'s from the uuid string.
        uuid.erase(remove(uuid.begin(), uuid.end(), '-'), uuid.end());

        std::string msb = "0x" + uuid.substr(0, 16);
        std::string lsb = "0x" + uuid.substr(16, 16);

        std::ostringstream os;
        os << strtoull(msb.c_str(), NULL, 0);
        uuid_msb = os.str();
        os.str("");

        os << strtoull(lsb.c_str(), NULL, 0);
        uuid_lsb = os.str();
    }

    virtual void CreatefmapConfig(const XmppStanza::XmppMessage *msg) {
        ConfigCassandraClientTest *cassandra_client =
            dynamic_cast<IFMapServerTest *>(ifmap_server_)->cassandra_client();

        if (!cassandra_client)
            return;

        if (msg->type != XmppStanza::IQ_STANZA)
            return;

        const XmppStanza::XmppMessageIq *iq;
        iq = static_cast<const XmppStanza::XmppMessageIq *>(msg);
        if (iq->iq_type.compare("set"))
            return;

        if (iq->action.compare("unsubscribe") &&
                iq->action.compare("subscribe")) {
            return;
        }

        const char* const vr_string = "virtual-router:";
        const char* const vm_string = "virtual-machine:";
        std::ostringstream config;
        std::string oper;
        std::string id_type;
        std::string id_name;
        string uuid_key;

        if (iq->node.compare(0, strlen(vm_string), vm_string) == 0) {
            id_type = "virtual-machine";
            id_name = std::string(iq->node, (strlen(vm_string)));
            uuid_key = id_name;
        } else if (iq->node.compare(0, strlen(vr_string), vr_string) == 0) {
            id_type = "virtual-router";
            id_name = std::string(iq->node, (strlen(vr_string)));
            boost::uuids::nil_generator nil;
            boost::uuids::name_generator gen(nil());
            boost::uuids::uuid uuid;
            uuid = gen(id_name);
            uuid_key = boost::uuids::to_string(uuid);
        } else {
            return;
        }

        int idx = cassandra_client->HashUUID(uuid_key);
        task_util::TaskFire(boost::bind(&IFMapXmppChannelTest::InjectConfig,
                                this, iq->action, uuid_key, id_type, id_name),
                            "cassandra::Reader", idx);
    }

    void InjectConfig(string action, string uuid_key, string id_type,
                      string id_name) {
        ConfigCassandraClientTest *cassandra_client =
            dynamic_cast<IFMapServerTest *>(ifmap_server_)->cassandra_client();
        if (!action.compare("unsubscribe")) {
            cassandra_client->HandleObjectDelete(uuid_key);
            return;
        }

        GenDb::ColList col_list;

        GenDb::DbDataValueVec *names1 = new GenDb::DbDataValueVec();
        names1->push_back(GenDb::DbDataValue(GenDb::Blob(
                         (const uint8_t *) "fq_name", 7)));
        GenDb::DbDataValueVec *values1 = new GenDb::DbDataValueVec();
        values1->push_back(GenDb::DbDataValue(string("[\"" + id_name + "\"]")));
        GenDb::DbDataValueVec *timestamps1 = new GenDb::DbDataValueVec();
        timestamps1->push_back(GenDb::DbDataValue(UTCTimestampUsec()));
        col_list.columns_.push_back(
            new GenDb::NewCol(names1, values1, 100, timestamps1));

        GenDb::DbDataValueVec *names2 = new GenDb::DbDataValueVec();
        names2->push_back(GenDb::DbDataValue(GenDb::Blob(
                         (const uint8_t *) "type", 4)));
        GenDb::DbDataValueVec *values2 = new GenDb::DbDataValueVec();
        values2->push_back(GenDb::DbDataValue(string("\"") + id_type + "\""));
        GenDb::DbDataValueVec *timestamps2 = new GenDb::DbDataValueVec();
        timestamps2->push_back(GenDb::DbDataValue(UTCTimestampUsec()));
        col_list.columns_.push_back(
            new GenDb::NewCol(names2, values2, 100, timestamps2));


        GenDb::DbDataValueVec *names3 = new GenDb::DbDataValueVec();
        names3->push_back(GenDb::DbDataValue(GenDb::Blob(
                (const uint8_t *) "prop:id_perms", 13)));
        std::string uuid_msb;
        std::string uuid_lsb;
        UuidToDecimal(uuid_key, uuid_msb, uuid_lsb);
        string id_perms = "{\"uuid\":{\"uuid_mslong\":\"" + uuid_msb +
            "\",\"uuid_lslong\":\"" + uuid_lsb +
            "\"},\"enable\":true,\"created\":null,\"last_modified\":null}";
        GenDb::DbDataValueVec *values3 = new GenDb::DbDataValueVec();
        values3->push_back(GenDb::DbDataValue(id_perms));
        GenDb::DbDataValueVec *timestamps3 = new GenDb::DbDataValueVec();
        timestamps3->push_back(GenDb::DbDataValue(UTCTimestampUsec()));
        col_list.columns_.push_back(
            new GenDb::NewCol(names3, values3, 100, timestamps3));

        cassandra_client->ProcessObjUUIDTableEntry(uuid_key, col_list);
    }

private:
    IFMapServer *ifmap_server_;
};

class PeerCloseManagerTest : public PeerCloseManager {
public:
    explicit PeerCloseManagerTest(IPeerClose *peer_close);
    ~PeerCloseManagerTest();
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServerTest *x, BgpServer *b);
    virtual ~BgpXmppChannelManagerMock();
    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel);
    BgpXmppChannel *channel() { return channel_; }
    void set_channel(BgpXmppChannel *channel) { channel_ = channel; }

private:
    BgpXmppChannel *channel_;
};

class BgpStressTestEvent {
public:

    //
    // List of test events
    //
    enum EventType {
        ADD_BGP_ROUTE = 1,
        DELETE_BGP_ROUTE,
        ADD_XMPP_ROUTE,
        DELETE_XMPP_ROUTE,
        BRING_UP_XMPP_AGENT,
        BRING_DOWN_XMPP_AGENT,
        CLEAR_XMPP_AGENT,
        SUBSCRIBE_ROUTING_INSTANCE,
        UNSUBSCRIBE_ROUTING_INSTANCE,
        SUBSCRIBE_CONFIGURATION,
        UNSUBSCRIBE_CONFIGURATION,
        ADD_BGP_PEER,
        DELETE_BGP_PEER,
        CLEAR_BGP_PEER,
        ADD_ROUTING_INSTANCE,
        DELETE_ROUTING_INSTANCE,
        ADD_ROUTE_TARGET,
        DELETE_ROUTE_TARGET,
        CHANGE_SOCKET_BUFFER_SIZE,
        SHOW_ALL_ROUTES,
        PAUSE,
        NUM_TEST_EVENTS = PAUSE - 1,
    };

    typedef std::map<std::string, const EventType> EventStringMap;

#define EVENT_INVALID (static_cast<BgpStressTestEvent::EventType>(0))

    static std::string ToString(EventType event);
    static EventType FromString(const std::string event);
    static void ReadEventsFromFile(std::string events_file);
    static EventType GetTestEvent();
    static int random(int limit);
    static std::vector<int> GetEventItems(int nitems, int inc = 0);
    static void clear_events();
    static std::vector<EventType> d_events_list_;
    static bool log_;
    static int count_;
    static std::vector<std::string> d_events_played_list_;
    static float GetEventWeightSum();
};

class SandeshServerTest : public SandeshServer {
public:
    SandeshServerTest(EventManager *evm) :
            SandeshServer(evm, SandeshConfig()) { }
    virtual ~SandeshServerTest() { }
    virtual bool ReceiveSandeshMsg(SandeshSession *session,
                       const SandeshMessage *msg, bool rsc) {
        return true;
    }

private:
};

class BgpNullPeer {
public:
    BgpNullPeer(BgpServerTest *server, int peer_id);
    BgpPeerTest *peer() { return peer_; }
    void set_peer(BgpPeerTest *peer) { peer_ = peer; }
    int peer_id() { return peer_id_; }

    bool ribout_creation_complete(Address::Family family) {
        return ribout_creation_complete_[family];
    }

    void ribout_creation_complete(Address::Family family, bool complete) {
        ribout_creation_complete_[family] = complete;
    }

    std::string name_;
    int peer_id_;
    BgpPeerTest *peer_;

private:
    std::vector<bool> ribout_creation_complete_;
};

typedef std::tr1::tuple<int, int, int, int, int, bool, bool> TestParams;

class BgpStressTest : public ::testing::TestWithParam<TestParams> {
protected:
    BgpStressTest() : thread_(&evm_) { }

    void IFMapInitialize(const std::string &hostname);
    void IFMapCleanUp();
    virtual void SetUp();
    virtual void TearDown();
    void AgentCleanup();
    void Cleanup();
    void SandeshShutdown();
    bool CheckRoutingInstance(int instance_id);
    void VerifyRoutingInstances();
    Ip4Prefix GetAgentRoute(int agent_id, int instance_id, int route_id);

    Inet6Prefix CreateAgentInet6Prefix(int agent_id, int instance_id,
                                       int route_id);
    void AddLocalPrefToAttr(BgpAttrSpec *attr_spec);
    void AddNexthopToAttr(BgpAttrSpec *attr_spec, int peer_id);
    void AddRouteTargetsToCommunitySpec(ExtCommunitySpec *commspec,
                                        int ntargets);
    void AddTunnelEncapToCommunitySpec(ExtCommunitySpec *commspec);
    void AddBgpInet6Route(int peer_id, int route_id, int num_targets);
    Inet6VpnPrefix CreateInet6VpnPrefix(std::string pre_prefix, int agent_id,
                                        int instance_id, int route_id);
    void AddBgpInet6VpnRoute(int peer_id, int route_id, int num_targets);

    void Configure(std::string config);
    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const std::string &from,
                                            const std::string &to,
                                            bool isClient);
    std::string GetInstanceName(int instance_id, int vn_id = 1);
    std::string GetInstanceConfig(int instance_id, int ntargets);
    std::string GetRouterConfig(int router_id, int peer_id,
                                bool skip_rtr_config);
    BgpAttr *CreatePathAttr();

    void AddBgpInetRoute(int family, int peer_id, int route_id, int ntargets);
    void AddBgpRoutesInBulk(std::vector<int> family, std::vector<int> peer_id,
                            std::vector<int> route_id, int ntargets);
    void AddBgpInetRouteInternal(int family, int peer_id, int ntargets,
                                 int route_id, std::string start_prefix,
                                 int label);
    void AddBgpRoutes(int family, int peer_id, int nroutes, int ntargets);
    void AddAllBgpRoutes(int nroutes, int ntargets);

    std::string GetAgentNexthop(int agent_id, int route_id);
    void AddXmppRoute(int instance_id, int agent_id, int route_id);
    void AddXmppRoute(std::vector<int> instance_ids, std::vector<int> agent_ids,
                      std::vector<int> route_ids);
    void AddXmppRoutes(int instance_id, int agent_id, int nroutes);
    void AddAllXmppRoutes(int ninstances, int nagents, int nroutes);

    void AddAllRoutes(int ninstances, int npeers, int nagents, int nroutes,
                      int ntargets);

    void DeleteBgpInetRoute(int family, int peer_id, int route_id,
                            int ntargets);
    void DeleteBgpRoutesInBulk(std::vector<int> family,
                               std::vector<int> peer_id,
                               std::vector<int> route_id, int ntargets);
    void DeleteBgpInetRouteInternal(int family, int peer_id, int route_id,
                                    std::string start_prefix, int ntargets);
    void DeleteBgpInet6VpnRoute(int peer_id, int route_id, int num_targets);
    void DeleteBgpRoutes(int family, int peer_id, int nroutes, int ntargets);
    void DeleteAllBgpRoutes(int nroutes, int ntargets, int npeers, int nagents);

    void DeleteXmppRoute(int instance_id, int agent_id, int route_id);
    void DeleteXmppRoute(std::vector<int> instance_ids,
                        std::vector<int> agent_ids, std::vector<int> route_ids);
    void DeleteXmppRoutes(int ninstances, int agent_id, int nroutes);
    void DeleteAllXmppRoutes(int ninstances, int nagents, int nroutes);

    void DeleteAllRoutes(int ninstances, int npeers, int nagents, int nroutes,
                         int ntargets);

    void VerifyControllerRoutes(int ninstances, int nagents, int count);
    void VerifyAgentRoutes(int nagents, int ninstances, int routes);
    size_t GetAllAgentRouteCount(int nagents, int ninstances);
    void VerifyXmppRouteNextHops();

    void InitParams();

    void SubscribeRoutingInstance(int agent_id, int instance_id,
                                  bool check_agent_state = true);
    void SubscribeRoutingInstance(std::vector<int> agent_ids,
                                  std::vector<int> instance_ids,
                                  bool check_agent_state = true);
    void SubscribeAgents(int ninstances, int nagents);
    void UnsubscribeRoutingInstance(int agent_id, int instance_id);
    void UnsubscribeRoutingInstance(std::vector<int> agent_ids,
                                    std::vector<int> instance_ids);
    void UnsubscribeAgents(int nagents, int ninstances);

    void SubscribeConfiguration(int agent_id, bool verify);
    void SubscribeConfiguration(std::vector<int> agent_ids, bool verify);
    void UnsubscribeConfiguration(int agent_id, bool verify);
    void UnsubscribeConfiguration(std::vector<int> agent_ids, bool verify);
    void SubscribeAgentsConfiguration(int agent_id, bool verify);
    void UnsubscribeAgentsConfiguration(int agent_id, bool verify);
    void VerifyConfiguration(int agent_id, int &pending);
    void VerifyNoConfiguration(int agent_id, int &pending);

    void BringUpXmppAgent(std::vector<int> agent_ids, bool verify_state);
    void BringUpXmppAgents(int nagents);
    void BringDownXmppAgent(BgpStressTestEvent::EventType event,
                            std::vector<int> agent_ids, bool verify_state);
    void BringDownXmppAgents(int nagents);
    void AddBgpPeer(int peer_id, bool verify_state);
    void AddBgpPeer(std::vector<int> peer_id, bool verify_state);
    void AddBgpPeers(int npeers);
    void DeleteBgpPeer(int peer_id, bool verify_state);
    void DeleteBgpPeer(std::vector<int> peer_id, bool verify_state);
    void DeleteBgpPeers(int npeers);
    void ClearBgpPeer(std::vector<int> peer_ids);
    void ClearBgpPeers(int npeers);
    void AddRouteTarget(int instance_id, int target);
    void RemoveRouteTarget(int instance_id, int target);
    void AddRoutingInstance(int instance_id, int ntargets);
    void AddRoutingInstance(std::vector<int> instance_ids, int ntargets);
    void AddRoutingInstances(int ninstances, int ntargets);
    void DeleteRoutingInstance(int instance_id, int ntargets);
    void DeleteRoutingInstance(std::vector<int> instance_ids, int ntargets);
    void DeleteRoutingInstances();
    bool IsAgentEstablished(test::NetworkAgentMock *agent);

    void VerifyPeer(BgpServerTest *server, BgpNullPeer *npeer);
    void VerifyPeers();
    void VerifyNoPeers();
    void VerifyNoPeer(int peer_id, std::string peer_name);
    void VerifyRibOutCreationCompletion();
    void UpdateSocketBufferSize();
    void ShowAllRoutes();
    void ShowNeighborStatistics();
    void Pause(std::string message);
    void ValidateShowNeighborStatisticsResponse(size_t expected_count,
                                                Sandesh *sandesh);
    void ValidateShowRouteSandeshResponse(Sandesh *sandesh);

    std::string GetAgentConfigName(int agent_id);
    std::string GetAgentVmUuid(int agent_id, int vm_id);
    std::string GetAgentName(int agent_id);
    bool XmppClientIsEstablished(const std::string &client_name);
    std::string GetEnetPrefix(std::string inet_prefix) const;

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    XmppServerTest *xmpp_server_test_;
    SandeshServerTest *sandesh_server_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> channel_manager_;
    boost::scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    RoutingInstance *rtinstance_;
    std::vector<bool> instances_;
    std::vector<BgpNullPeer *> peers_;
    std::vector<BgpServerTest *> peer_servers_;
    std::vector<test::NetworkAgentMock *> xmpp_agents_;
    std::vector<BgpXmppChannel *> xmpp_peers_;
    int n_families_;
    std::vector<Address::Family> families_;
    int n_instances_;
    int n_peers_;
    int n_routes_;
    int n_agents_;
    int n_targets_;
    bool xmpp_close_from_control_node_;
    bool xmpp_auth_enabled_;
    int socket_buffer_size_;
    bool sandesh_response_validation_complete_;
    boost::scoped_ptr<BgpSandeshContext> sandesh_context_;

    DB *config_db_;
    DBGraph *config_graph_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<IFMapChannelManager> ifmap_channel_mgr_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
};

#endif

