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

static int get_row_result_;
static int get_row_have_index_;
static int get_multirow_result_;
static int get_multirow_result_index_;
static int asked = 8;
static vector<size_t> get_row_sizes = boost::assign::list_of
    (0)(asked/2)(asked-1)(asked)(asked+1)(asked*3/2)(asked*2)(asked*2+1)(asked*3)(asked*3 + 1)(asked * 4);
static vector<string> type_choices = boost::assign::list_of
    ("type")("fq_name")("prop:display_name")("junk");
static vector<string> value_choices = boost::assign::list_of
    ("\"virtual_network\"")("[\"default-domain\",\"admin\",\"vn1\"]")("\"vn1\"")("\"junkv\"");
static vector<vector<string> > type_subsets = GetSubSets(type_choices);
static vector<vector<string> > value_subsets = GetSubSets(value_choices);

static int db_init_result_;
static int set_table_space_result_;
static int use_column_family_uuid_result_;
static int use_column_family_fqn_result_;

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
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000000B",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000000C",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000000D",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000000E",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000000F",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000010",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000011",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000012",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000013",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000014",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000015",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000016",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000017",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000018",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000019",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000001A",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000001B",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000001C",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000001D",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000001E",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-00000000001F",
    "default-domain:admin:vn1:vn1:7acf5d97-d48b-4409-a30c-000000000020",
};
static size_t fq_names = sizeof(fq_name_uuids)/sizeof(fq_name_uuids[0]);

static tbb::mutex mutex_;
static std::vector<std::string> given_uuids;
static std::vector<std::string> received_uuids;
static bool empty_fqnames_db;
static uint64_t timestamp_;

class CqlIfTest : public cass::cql::CqlIf {
public:
    CqlIfTest(EventManager *evm, const std::vector<std::string> &cassandra_ips,
              int cassandra_port, const std::string &cassandra_user,
              const std::string &cassandra_password) :
            cass::cql::CqlIf(evm, cassandra_ips, cassandra_port, cassandra_user,
                             cassandra_password) {
    }

    virtual bool Db_Init() { return db_init_result_-- < 1; }
    virtual void Db_Uninit() { }
    virtual bool Db_SetTablespace(const std::string &tablespace) {
        return set_table_space_result_-- < 1;
    }
    virtual bool Db_UseColumnfamily(const std::string &cfname) {
        if (cfname == ConfigCassandraClient::kUuidTableName)
            return use_column_family_uuid_result_-- < 1;
        assert(cfname == ConfigCassandraClient::kFqnTableName);
        return use_column_family_fqn_result_-- < 1;
    }
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

        if (get_row_result_-- > 0)
            return false;

        if (empty_fqnames_db || !get_row_sizes[get_row_have_index_])
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
        while (i < get_row_sizes[get_row_have_index_] && i < fq_names &&
                column_name >= fq_name_uuids[i]) {
            i++;
        }

        if (i >= get_row_sizes[get_row_have_index_] || i >= fq_names)
            return true;

        for (int pushed = 0; i < get_row_sizes[get_row_have_index_]; i++) {
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
        if (!timestamp_)
            timestamp_ = UTCTimestampUsec();
        ts->push_back(timestamp_);
        return ts;
    }

    virtual bool Db_GetMultiRow(GenDb::ColListVec *out,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey,
        const GenDb::ColumnNameRange &crange,
        const GenDb::FieldNamesToReadVec &read_vec) {
        if (get_multirow_result_-- > 0)
            return false;

        BOOST_FOREACH(const GenDb::DbDataValueVec &key, v_rowkey) {
            GenDb::ColList *val = new GenDb::ColList();
            out->push_back(val);
            val->rowkey_ = key;
            for (size_t i = 0;
                 i < type_subsets[get_multirow_result_index_].size(); i++) {
                string type = type_subsets[get_multirow_result_index_][i];
                string value = value_subsets[get_multirow_result_index_][i];

                GenDb::DbDataValueVec *tp = new GenDb::DbDataValueVec();
                tp->push_back(GenDb::Blob(reinterpret_cast<const uint8_t *>(
                                type.c_str()), type.size()));

                GenDb::DbDataValueVec *vl = new GenDb::DbDataValueVec();
                vl->push_back(value);

                GenDb::DbDataValueVec *ts = GetTimeStamp();
                val->columns_.push_back(new GenDb::NewCol(tp, vl, 10, ts));
            }

            // If both type and fq-names are present, then this would be valid.
            if (std::find(type_subsets[get_multirow_result_index_].begin(),
                    type_subsets[get_multirow_result_index_].end(),
                    "type") == type_subsets[get_multirow_result_index_].end())
                continue;
            if (std::find(type_subsets[get_multirow_result_index_].begin(),
                type_subsets[get_multirow_result_index_].end(),
                "fq_name") == type_subsets[get_multirow_result_index_].end())
                continue;

            const GenDb::DbDataValue &dname(key[0]);
            assert(dname.which() == GenDb::DB_VALUE_BLOB);
            GenDb::Blob dname_blob(boost::get<GenDb::Blob>(dname));
            string uuid = string(reinterpret_cast<const char *>(
                                 dname_blob.data()), dname_blob.size());
            {
                tbb::mutex::scoped_lock lock(mutex_);
                given_uuids.push_back(uuid);
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

private:
    virtual void ParseContextAndPopulateIFMapTable(
        const string &uuid_key, const ConfigCassandraParseContext &context,
        const CassColumnKVVec &cass_data_vec) {
        if (cass_data_vec.empty())
            return;
        tbb::mutex::scoped_lock lock(mutex_);
        received_uuids.push_back(uuid_key);
    }
    virtual uint32_t GetFQNameEntriesToRead() const { return asked; }
    virtual uint32_t GetNumReadRequestToBunch() const { return asked; }
    virtual const int GetMaxRequestsToYield() const { return 4; }
    virtual const uint64_t GetInitRetryTimeUSec() const { return 10; }
    virtual bool SkipTimeStampCheckForTypeAndFQName() const { return false; }
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

typedef std::tr1::tuple<int, size_t, int, size_t> TestParams;
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
        get_row_result_ = std::tr1::get<0>(GetParam());
        get_row_have_index_ = std::tr1::get<1>(GetParam());
        get_multirow_result_ = std::tr1::get<2>(GetParam());
        get_multirow_result_index_ = std::tr1::get<3>(GetParam());
        empty_fqnames_db = false;
        timestamp_ = 0;
        db_init_result_ = 2;
        set_table_space_result_ = 2;
        use_column_family_uuid_result_ = 2;
        use_column_family_fqn_result_ = 2;

        IFMapLinkTable_Init(&db_, &graph_);
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
        sort(given_uuids.begin(), given_uuids.end());
        sort(received_uuids.begin(), received_uuids.end());
        return received_uuids == given_uuids;
    }

    bool IsNoEntryProcessed() {
        tbb::mutex::scoped_lock lock(mutex_);
        return received_uuids.empty();
    }

    bool IsConnectionUp () {
        ConfigDBConnInfo status;
        config_client_manager_->config_db_client()->GetConnectionInfo(status);
        return status.connection_status;
    }

    void Clear() {
        tbb::mutex::scoped_lock lock(mutex_);
        given_uuids.clear();
        received_uuids.clear();
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManagerTest> config_client_manager_;
};


TEST_P(ConfigCassandraClientTest, Basic) {
    config_client_manager_->Initialize();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(IsConnectionUp());
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());

    // Initialze the db again, with same time stamp and verify that no new
    // updates are generated;
    Clear();
    config_client_manager_->set_end_of_rib_computed(false);
    config_client_manager_->Initialize();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(IsNoEntryProcessed());

    // Initialze the db again, with newer time stamp.
    timestamp_ = 0;
    Clear();
    config_client_manager_->set_end_of_rib_computed(false);
    config_client_manager_->Initialize();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());

    // Do not return any data from fqname table resulting in all deletes.
    empty_fqnames_db = true;
    Clear();
    config_client_manager_->set_end_of_rib_computed(false);
    config_client_manager_->Initialize();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());
}

INSTANTIATE_TEST_CASE_P(ConfigCassandraClientTestWithParams,
                        ConfigCassandraClientTest,
::testing::Combine(
    ::testing::Range<int>(0, 2), // Db_GetRow retry count
    ::testing::Range<size_t>(0, get_row_sizes.size()), // Db_GetRow
    ::testing::Range<int>(0, 2), // Db_GetMultiRow retry count
    ::testing::Range<size_t>(0, type_subsets.size()) // Db_GetMultiRow
));

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
