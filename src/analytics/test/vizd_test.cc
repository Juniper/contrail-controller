/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "testing/gunit.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "base/util.h"
#include "base/parse_object.h"
#include "base/contrail_ports.h"
#include "base/test/task_test_util.h"
#include "io/test/event_manager_test.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_http.h>
#include <sandesh/sandesh.h>

#include "../viz_collector.h"
#include "../ruleeng.h"

#include "test/viz_collector_test_types.h"
#include "test/vizd_test_types.h"

#include "cdb_if_mock.h"
#include "OpServerProxyMock.h"
#include "viz_constants.h"

using namespace std;

using ::testing::Return;
using ::testing::Field;
using ::testing::AnyOf;
using ::testing::AnyNumber;
using ::testing::_;
using ::testing::Eq;
using ::testing::ElementsAre;

string sourcehost = "127.0.0.1";
string collector_server = "127.0.0.1";
short collector_port = 8180; //random server port

#define WAIT_FOR(Cond)                  \
do {                                    \
    for (int i = 0; i < 10000; i++) {  \
        if (Cond) break;                \
        usleep(1000);                   \
    }                                   \
    EXPECT_TRUE(Cond);                  \
} while (0)

class GeneratorTest {
public:
    GeneratorTest() : 
        evm_(new EventManager()),
        thread_(new ServerThread(evm_.get())) {
        Sandesh::InitGenerator("GeneratorTest", sourcehost, evm_.get(),
                collector_server, collector_port, 0, NULL, true);

        thread_->Start();

        WAIT_FOR(Sandesh::SendReady());
    }

    ~GeneratorTest() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    void Shutdown() {
        task_util::WaitForIdle();
        Sandesh::Uninit();
    }

    void SendMessageAsync() {
        SANDESH_ASYNC_TEST1_SEND(100, "sat1string100");
        SANDESH_ASYNC_TEST1_SEND(101, "sat1string101");
        SANDESH_ASYNC_TEST1_SEND(102, "sat1string102");
    }

    void SendMessageUVETrace() {
        UveVirtualNetworkConfig uvevn;
        uvevn.set_name("abc-corp:vn02");
        uvevn.set_total_interfaces(10);
        uvevn.set_total_virtual_machines(5);
        uvevn.set_total_acl_rules(60);

        std::vector<std::string> vcn;
        uvevn.set_connected_networks(vcn);
        UveVirtualNetworkConfigTrace::Send(uvevn);

        UveVirtualNetworkAgent uvena;
        uvena.set_name("abc-corp:vn02");
        uvena.set_in_tpkts(40);
        uvena.set_total_acl_rules(55);

        std::vector<UveInterVnStats> vvn;
        UveInterVnStats vnstat;
        vnstat.set_other_vn("abc-corp:map-reduce-02");
        vnstat.set_tpkts(10);
        vnstat.set_bytes(1200);
        vvn.push_back(vnstat);

        uvena.set_in_stats(vvn);
        UveVirtualNetworkAgentTrace::Send(uvena);
    }

    std::auto_ptr<EventManager> evm_;
    std::auto_ptr<ServerThread> thread_;
};

class VizdTest : public ::testing::Test {
public:
    VizdTest() {
        evm_.reset(new EventManager());
        thread_.reset(new ServerThread(evm_.get()));

        osp_mock_ = (new OpServerProxyMock(evm_.get()));
        dbif_mock_ = (new CdbIfMock(evm_.get()->io_service(), boost::bind(&VizdTest::DbErrorHandlerFn, this)));
        DbHandler *db_handler(new DbHandler(dbif_mock_));
        Ruleeng *ruleeng(new Ruleeng(db_handler, osp_mock_));
        collector_ = new Collector(evm_.get(), collector_port, db_handler, ruleeng);

        analytics_.reset(new VizCollector(evm_.get(), db_handler, ruleeng,
                         collector_, osp_mock_));

        thread_->Start();
    }

    ~VizdTest() {
        task_util::WaitForIdle();
        WAIT_FOR(!analytics_->GetCollector()->HasSessions());
        task_util::WaitForIdle();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    void DbErrorHandlerFn() {
        assert(0);
    }

    CdbIfMock *dbif_mock() {
        return dbif_mock_;
    }

    OpServerProxyMock *osp_mock() {
        return osp_mock_;
    }


    virtual void SetUp() {
    }

    virtual void TearDown() {
        analytics_->Shutdown();
        task_util::WaitForIdle();

        if (collector_) {
            TcpServerManager::DeleteServer(collector_);
            collector_ = NULL;
        }

        SandeshHttp::Uninit();
        task_util::WaitForIdle();
    }

    std::auto_ptr<ServerThread> thread_;
    std::auto_ptr<EventManager> evm_;
    CdbIfMock *dbif_mock_;
    OpServerProxyMock *osp_mock_;
    std::auto_ptr<VizCollector> analytics_;
    Collector *collector_;
};

TEST_F(VizdTest, MessagesTest) {
    EXPECT_CALL(*dbif_mock(),
            Db_Init())
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddSetTablespace(g_viz_constants.COLLECTOR_KEYSPACE))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_GetColumnFamilies(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return());

    EXPECT_CALL(*dbif_mock(),
            Db_GetRangeSlices(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumnfamily(::testing::_))
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));

    analytics_->Init();

    //SANDESH_ASYNC_TEST1_SEND(100, "sat1string100");
    //SANDESH_ASYNC_TEST1_SEND(101, "sat1string101");
    //SANDESH_ASYNC_TEST1_SEND(102, "sat1string102");
    std::string temp_str0(sizeof(uint32_t), 0);
    put_value((uint8_t *)temp_str0.c_str(), sizeof(uint32_t), 1);

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                Field(&GenDb::Column::columns_,
                    ElementsAre(
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SOURCE),
                            Field(&GenDb::ColElement::elem_value, "127.0.0.1")),
                        ::testing::_, //NAMESPACE
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MODULE),
                            Field(&GenDb::ColElement::elem_value, "GeneratorTest")),
                        ::testing::_, //TIMESTAMP
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MESSAGE_TYPE),
                            Field(&GenDb::ColElement::elem_value, "SandeshAsyncTest1")),
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SEQUENCE_NUM),
                            Field(&GenDb::ColElement::elem_value, temp_str0)),
                        ::testing::_, ::testing::_, ::testing::_)))))
        .Times(1)
        .WillOnce(Return(true));

    std::string temp_str1(sizeof(uint32_t), 0);
    put_value((uint8_t *)temp_str1.c_str(), sizeof(uint32_t), 2);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                Field(&GenDb::Column::columns_,
                    ElementsAre(
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SOURCE),
                            Field(&GenDb::ColElement::elem_value, "127.0.0.1")),
                        ::testing::_, //NAMESPACE
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MODULE),
                            Field(&GenDb::ColElement::elem_value, "GeneratorTest")),
                        ::testing::_, //TIMESTAMP
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MESSAGE_TYPE),
                            Field(&GenDb::ColElement::elem_value, "SandeshAsyncTest1")),
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SEQUENCE_NUM),
                            Field(&GenDb::ColElement::elem_value, temp_str1)),
                        ::testing::_, ::testing::_, ::testing::_)))))
        .Times(1)
        .WillOnce(Return(true));

    std::string temp_str2(sizeof(uint32_t), 0);
    put_value((uint8_t *)temp_str2.c_str(), sizeof(uint32_t), 3);
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                Field(&GenDb::Column::columns_,
                    ElementsAre(
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SOURCE),
                            Field(&GenDb::ColElement::elem_value, "127.0.0.1")),
                        ::testing::_, //NAMESPACE
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MODULE),
                            Field(&GenDb::ColElement::elem_value, "GeneratorTest")),
                        ::testing::_, //TIMESTAMP
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MESSAGE_TYPE),
                            Field(&GenDb::ColElement::elem_value, "SandeshAsyncTest1")),
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SEQUENCE_NUM),
                            Field(&GenDb::ColElement::elem_value, temp_str2)),
                        ::testing::_, ::testing::_, ::testing::_)))))
        .Times(1)
        .WillOnce(Return(true));

    // instantiate a new generator
    GeneratorTest gentest;
    gentest.SendMessageAsync();

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                Field(&GenDb::Column::columns_,
                    ElementsAre(
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SOURCE),
                            Field(&GenDb::ColElement::elem_value, "127.0.0.1")),
                        ::testing::_, //NAMESPACE
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MODULE),
                            Field(&GenDb::ColElement::elem_value, "GeneratorTest")),
                        ::testing::_, //TIMESTAMP
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MESSAGE_TYPE),
                            Field(&GenDb::ColElement::elem_value, "UveVirtualNetworkConfigTrace")),
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SEQUENCE_NUM),
                            Field(&GenDb::ColElement::elem_value, temp_str0)),
                        ::testing::_, ::testing::_, ::testing::_)))))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(AllOf(Field(&GenDb::Column::cfname_, g_viz_constants.COLLECTOR_GLOBAL_TABLE),
                Field(&GenDb::Column::columns_,
                    ElementsAre(
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SOURCE),
                            Field(&GenDb::ColElement::elem_value, "127.0.0.1")),
                        ::testing::_, //NAMESPACE
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MODULE),
                            Field(&GenDb::ColElement::elem_value, "GeneratorTest")),
                        ::testing::_, //TIMESTAMP
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.MESSAGE_TYPE),
                            Field(&GenDb::ColElement::elem_value, "UveVirtualNetworkAgentTrace")),
                        AllOf(Field(&GenDb::ColElement::elem_name, g_viz_constants.SEQUENCE_NUM),
                            Field(&GenDb::ColElement::elem_value, temp_str0)),
                        ::testing::_, ::testing::_, ::testing::_)))))
        .Times(1)
        .WillOnce(Return(true));

    // this includes all calls
    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_,
                    g_viz_constants.MESSAGE_TABLE_SOURCE)))
        .Times(5)
        .WillRepeatedly(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_,
                    g_viz_constants.MESSAGE_TABLE_MODULE_ID)))
        .Times(5)
        .WillRepeatedly(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_,
                    g_viz_constants.MESSAGE_TABLE_MESSAGE_TYPE)))
        .Times(5)
        .WillRepeatedly(Return(true));

    EXPECT_CALL(*dbif_mock(),
            Db_AddColumn(Field(&GenDb::Column::cfname_, "ObjectVNTable")))
        .Times(2)
        .WillRepeatedly(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkConfig", "total_interfaces",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_, 
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkConfig", "total_virtual_machines",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_,
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkConfig", "connected_networks",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_,
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkConfig", "total_acl_rules",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_,
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkAgent", "in_tpkts",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_,
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkAgent", "in_stats",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_,
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    EXPECT_CALL(*osp_mock(),
            UVEUpdate("UveVirtualNetworkAgent", "total_acl_rules",
                      "127.0.0.1", "GeneratorTest",
                      "abc-corp:vn02", ::testing::_, ::testing::_,
                      ::testing::_, ::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(Return(true));

    gentest.SendMessageUVETrace();

    //last test shutdown generator
    gentest.Shutdown();
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}

