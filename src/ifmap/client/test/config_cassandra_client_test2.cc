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

static int asked = 8;
static vector<string> type_choices = boost::assign::list_of
    ("type")("fq_name")("prop:display_name")("junk");
static vector<string> value_choices = boost::assign::list_of
    ("\"virtual_network\"")("[\"default-domain\",\"admin\",\"vn1\"]")("\"vn1\"")("\"junkv\"");

static const string fq_name_uuids[] = {
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000001",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000002",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000003",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000004",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000005",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000006",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000007",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000008",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000009",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000000A",
};
static size_t fq_names = sizeof(fq_name_uuids)/sizeof(fq_name_uuids[0]);

static tbb::mutex mutex_;
static std::vector<std::string> updated;
static std::vector<std::string> deleted;

class CqlIfTest : public cass::cql::CqlIf {
public:
    CqlIfTest(EventManager *evm, const std::vector<std::string> &cassandra_ips,
              int cassandra_port, const std::string &cassandra_user,
              const std::string &cassandra_password) :
            cass::cql::CqlIf(evm, cassandra_ips, cassandra_port, cassandra_user,
                             cassandra_password) {
    }

    virtual bool Db_Init() { return true; }
    virtual void Db_Uninit() { }
    virtual bool Db_SetTablespace(const std::string &tablespace) { return true;}
    virtual bool Db_UseColumnfamily(const std::string &cfname) { return true; }
    virtual bool Db_GetRow(GenDb::ColList *out, const std::string &cfname,
        const GenDb::DbDataValueVec &rowkey,
        GenDb::DbConsistency::type dconsistency,
        const GenDb::ColumnNameRange &crange,
        const GenDb::FieldNamesToReadVec &read_vec) {
        assert(crange.count_);

        const GenDb::DbDataValue &type(rowkey[0]);
        assert(type.which() == GenDb::DB_VALUE_BLOB);
        GenDb::Blob type_blob(boost::get<GenDb::Blob>(type));
        string type_name =
            string(reinterpret_cast<const char *>(type_blob.data()),
                                                  type_blob.size());
        if (type_name != "virtual_network")
            return true;

        string column_name;
        if (!crange.start_.empty()) {
            const GenDb::DbDataValue &dname(crange.start_[0]);
            assert(dname.which() == GenDb::DB_VALUE_BLOB);
            GenDb::Blob dname_blob(boost::get<GenDb::Blob>(dname));
            column_name = string(reinterpret_cast<const char *>(
                                    dname_blob.data()), dname_blob.size());
        }

        // Find the first entry > column_name.
        size_t i = 0;
        while (i < fq_names && column_name >= fq_name_uuids[i])
            i++;
        if (i >= fq_names)
            return true;

        for (int pushed = 0; i < fq_names; i++) {
            GenDb::DbDataValueVec *n = new GenDb::DbDataValueVec();
            n->push_back(GenDb::Blob(reinterpret_cast<const uint8_t *>
                (fq_name_uuids[i].c_str()), fq_name_uuids[i].size()));
            out->columns_.push_back(new GenDb::NewCol(n, NULL, 10));
            if (++pushed >= (int) crange.count_)
                break;
        }
        return true;
    }

    GenDb::DbDataValueVec *GetTimeStamp() {
        GenDb::DbDataValueVec *ts = new GenDb::DbDataValueVec();
        tbb::mutex::scoped_lock lock(mutex_);
        ts->push_back(UTCTimestampUsec());
        return ts;
    }

    virtual bool Db_GetMultiRow(GenDb::ColListVec *out,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey,
        const GenDb::ColumnNameRange &crange,
        const GenDb::FieldNamesToReadVec &read_vec) {

        BOOST_FOREACH(const GenDb::DbDataValueVec &key, v_rowkey) {
            GenDb::ColList *val = new GenDb::ColList();
            out->push_back(val);
            val->rowkey_ = key;
            for (size_t i = 0; i < type_choices.size(); i++) {
                string type = type_choices[i];
                string value = value_choices[i];

                GenDb::DbDataValueVec *tp = new GenDb::DbDataValueVec();
                tp->push_back(GenDb::Blob(reinterpret_cast<const uint8_t *>(
                                type.c_str()), type.size()));

                GenDb::DbDataValueVec *vl = new GenDb::DbDataValueVec();
                vl->push_back(value);

                GenDb::DbDataValueVec *ts = GetTimeStamp();
                val->columns_.push_back(new GenDb::NewCol(tp, vl, 10, ts));
            }
        }
        return true;
    }
    virtual std::vector<GenDb::Endpoint> Db_GetEndpoints() const {
        return std::vector<GenDb::Endpoint>();
    }
};

class ConfigCassandraClientMock : public ConfigCassandraClient {
public:
    ConfigCassandraClientMock(ConfigClientManager *mgr, EventManager *evm,
        const IFMapConfigOptions &options, ConfigJsonParser *in_parser,
        int num_workers) : ConfigCassandraClient(mgr, evm, options, in_parser,
            num_workers) {
    }
    virtual bool BulkDataSync() {
        return ConfigCassandraClient::BulkDataSync();
    }

    void DisableWorkQueues(bool flag) {
        for (int i = 0; i < num_workers(); i++)
            obj_process_queue()[i]->set_disable(flag);
    }

private:
    virtual void PraseAndEnqueueToIFMapTable(
        const string &uuid_key, const ConfigCassandraParseContext &context,
        const CassColumnKVVec &cass_data_vec) {
        if (cass_data_vec.empty())
            return;
        tbb::mutex::scoped_lock lock(mutex_);
        updated.push_back(uuid_key);
    }
    virtual uint32_t GetFQNameEntriesToRead() const { return asked; }
    virtual uint32_t GetNumReadRequestToBunch() const { return asked; }
    virtual const int GetMaxRequestsToYield() const { return 4; }
    virtual const uint64_t GetInitRetryTimeUSec() const { return 10; }
    virtual bool SkipTimeStampCheckForTypeAndFQName() const { return false; }
    virtual void EnqueuDelete(const string &uuid,
            ConfigClientManager::RequestList req_list) const {
        tbb::mutex::scoped_lock lock(mutex_);
        deleted.push_back(uuid);
    }
};

class ConfigClientManagerTest : public ConfigClientManager {
public:
    ConfigClientManagerTest(EventManager *evm,
        IFMapServer *ifmap_server, string hostname, string module_name,
        const IFMapConfigOptions& config_options) :
                ConfigClientManager(evm, ifmap_server, hostname, module_name,
                                    config_options) {
    }
    void set_end_of_rib_computed(bool flag) { end_of_rib_computed_ = flag; }
};

typedef std::tr1::tuple<int, bool> TestParams;
class ConfigCassandraClientTest : public ::testing::TestWithParam<TestParams> {
protected:
    ConfigCassandraClientTest() :
        thread_(&evm_),
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        ifmap_server_(new IFMapServer(&db_, &graph_, evm_.io_service())),
        config_client_manager_(new ConfigClientManagerTest(&evm_,
            ifmap_server_.get(), "localhost", "config-test", config_options_)) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        Clear();
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_Server_ModuleInit(&db_, &graph_);
        thread_.Start();
        task_util::WaitForIdle();
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

    bool IsAllExpectedEntriesProcessed() {
        tbb::mutex::scoped_lock lock(mutex_);
        sort(updates.begin(), updates.end());
        sort(deletes.begin(), deletes.end());
        sort(updated.begin(), updated.end());
        sort(deleted.begin(), deleted.end());
        return updates == updated && deletes == deleted;
    }

    bool IsConnectionUp () {
        ConfigDBConnInfo status;
        config_client_manager_->config_db_client()->GetConnectionInfo(status);
        return status.connection_status;
    }

    void Clear() {
        tbb::mutex::scoped_lock lock(mutex_);
        updates.clear();
        updated.clear();
        deletes.clear();
        deleted.clear();
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManagerTest> config_client_manager_;
    vector<string> updates;
    vector<string> deletes;
};


TEST_P(ConfigCassandraClientTest, Basic) {
    config_client_manager_->Initialize();
    ConfigCassandraClientMock *mock = dynamic_cast<ConfigCassandraClientMock *>(
        config_client_manager_->config_db_client());
    task_util::WaitForIdle();
    for (size_t i = 0; i < fq_names; i++) {
        vector<string> tokens;
        boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
        updates.push_back(tokens[4]);
    }
    TASK_UTIL_EXPECT_TRUE(IsConnectionUp());
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());
    Clear();

    int pattern = std::tr1::get<0>(GetParam());
    bool pause_work_queue = std::tr1::get<1>(GetParam());
    int repeat = 1;

    // Enqueue combinations of updates and deletes with duplicates.
    // Inject duplicate uuids when work queue processing is disabled, as we
    // can be sure duplicate entries get pruned (in uuid_read_set_)
    if (pause_work_queue) {
        mock->DisableWorkQueues(true);
        repeat = 3;
    }

    // Loop to duplicate across different uuids
    for (int i = 0; i < repeat; i++) {
        for (size_t j = 0; j < fq_names; j++) {
            vector<string> tokens;
            boost::split(tokens, fq_name_uuids[j], boost::is_any_of(":"));
            string uuid = tokens[4];

            // Loop to duplicate adjancent uuids
            for (int k = 0; k < repeat; k++) {
                if (pattern & (1 << j)) {
                    config_client_manager_->config_amqp_client()->
                        EnqueueUUIDRequest("DELETE", "virtual_network", uuid);
                    if (!i && !k)
                        deletes.push_back(uuid);
                } else {
                    config_client_manager_->config_amqp_client()->
                        EnqueueUUIDRequest("UPDATE", "virtual_network", uuid);
                    if (!i && !k)
                        updates.push_back(uuid);
                }
            }
        }
    }

    task_util::WaitForIdle();
    if (pause_work_queue) {
        TASK_UTIL_EXPECT_FALSE(IsAllExpectedEntriesProcessed());
        mock->DisableWorkQueues(false);
    }
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());

    config_client_manager_->set_end_of_rib_computed(false);
    for (size_t i = 0; i < fq_names; i++) {
        vector<string> tokens;
        boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
        config_client_manager_->config_amqp_client()->EnqueueUUIDRequest(
            "DELETE", "virtual_network", tokens[4]);
    }
    task_util::WaitForIdle();
}

INSTANTIATE_TEST_CASE_P(ConfigCassandraClientTestWithParams,
    ConfigCassandraClientTest,
    ::testing::Combine(
        ::testing::Range<int>(0, (1 << fq_names) - 1), // update/delete bit-mask
        ::testing::Bool())); // Pause work queue or not

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigAmqpClient::set_disable(true);
    IFMapFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientMock *>());
    IFMapFactory::Register<cass::cql::CqlIf>(boost::factory<CqlIfTest *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
