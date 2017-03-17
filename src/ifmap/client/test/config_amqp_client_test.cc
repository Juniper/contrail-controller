/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/foreach.hpp>
#include <fstream>

#include <string>
#include <vector>

#include <tbb/mutex.h>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "database/cassandra/cql/cql_if.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_cass2json_adapter.h"
#include "ifmap/client/config_cassandra_client.h"
#include "ifmap/client/config_client_manager.h"
#include "ifmap/client/config_json_parser.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
static tbb::atomic<int> consume_count_;
static tbb::atomic<int> bind_queue_count_;

class AmqpClientInterfaceTest : public ConfigAmqpChannel {
public:
    AmqpClientInterfaceTest() : ConfigAmqpChannel() {
    }
    virtual ~AmqpClientInterfaceTest() { }
    virtual AmqpClient::Channel::ptr_t CreateFromUri(std::string uri) {
        return AmqpClient::Channel::ptr_t();
    }
    virtual void DeclareExchange(const std::string &exchange_name,
                                 const std::string &exchange_type, bool passive,
                                 bool durable, bool auto_delete) {
    }
    virtual void DeleteQueue(const std::string &queue_name, bool if_unused,
                             bool if_empty) {
    }
    virtual std::string DeclareQueue(const std::string &queue_name,
            bool passive, bool durable, bool exclusive, bool auto_delete) {
        return "";
    }
    virtual void BindQueue(const std::string &queue_name,
                           const std::string &exchange_name,
                           const std::string &routing_key = "") {
        bind_queue_count_++;
    }
    virtual std::string BasicConsume(const std::string &queue,
                                     const std::string &consumer_tag,
                                     bool no_local, bool no_ack, bool exclusive,
                                     boost::uint16_t message_prefetch_count) {
        return "";
    }
    virtual bool BasicConsumeMessage(const std::string &consumer_tag,
                                     AmqpClient::Envelope::ptr_t &envelope,
                                     int timeout) {
        return !(++consume_count_ % 2); // Simulate connection flips..
    }
    virtual void BasicAck(const AmqpClient::Envelope::ptr_t &message) {
    }
};

class ConfigClientManagerTest : public ConfigClientManager {
public:
    ConfigClientManagerTest(EventManager *evm,
        IFMapServer *ifmap_server, string hostname, string module_name,
        const IFMapConfigOptions& config_options) :
                ConfigClientManager(evm, ifmap_server, hostname, module_name,
                                    config_options, true) {
        end_of_rib_computed_ = true;
    }
};

class ConfigAmqpClientTest : public ::testing::Test {
protected:
    ConfigAmqpClientTest() :
        thread_(&evm_),
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        ifmap_server_(new IFMapServer(&db_, &graph_, evm_.io_service())),
        config_client_manager_(new ConfigClientManagerTest(&evm_,
            ifmap_server_.get(), "localhost", "config-test",
            GetConfigOptions())) {
    }

    IFMapConfigOptions GetConfigOptions() {
        config_options_.rabbitmq_server_list.push_back("127.0.0.1:100");
        config_options_.rabbitmq_user = "foo";
        config_options_.rabbitmq_password = "bar";
        config_options_.rabbitmq_vhost = "test";
        return config_options_;
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_Server_ModuleInit(&db_, &graph_);
        thread_.Start();
    }

    virtual void TearDown() {
        ifmap_server_->Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        config_client_manager_->config_json_parser()->MetadataClear("vnc_cfg");
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    IFMapConfigOptions config_options_;
    boost::scoped_ptr<ConfigClientManagerTest> config_client_manager_;
};


TEST_F(ConfigAmqpClientTest, Basic) {
    ConfigAmqpConnInfo conn_info;
    config_client_manager_->config_amqp_client()->GetConnectionInfo(conn_info);
    TASK_UTIL_EXPECT_EQ(string("amqp://" + config_options_.rabbitmq_user + ":" +
        config_options_.rabbitmq_password + "@" + "127.0.0.1:100/" +
        config_options_.rabbitmq_vhost), conn_info.url);

    // Verify that BasicConsumeMessage() gets repeatedly called.
    TASK_UTIL_EXPECT_LT(100, consume_count_);

    // Terminate rabbit mq poll loop.
    config_client_manager_->config_amqp_client()->set_terminate(true);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(consume_count_/2 - bind_queue_count_ < 3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    IFMapFactory::Register<ConfigAmqpChannel>(
        boost::factory<AmqpClientInterfaceTest *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
