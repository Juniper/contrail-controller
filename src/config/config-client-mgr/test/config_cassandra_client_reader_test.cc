/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/foreach.hpp>
#include <fstream>
#include <iostream>
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
#include "config/config-client-mgr/config_amqp_client.h"
#include "config/config-client-mgr/config_cass2json_adapter.h"
#include "config/config-client-mgr/config_cassandra_client.h"
#include "config/config-client-mgr/config_client_manager.h"
#include "config/config-client-mgr/config_json_parser_base.h"
#include "config/config-client-mgr/config_client_options.h"
#include "config/config-client-mgr/config_factory.h"
#include "config/config-client-mgr/config_client_show_types.h"
#include "io/test/event_manager_test.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using std::equal;
using std::string;
using std::set;
using std::vector;

using ::testing::InitGoogleTest;
using ::testing::Range;
using ::testing::Test;
using ::testing::WithParamInterface;

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
static size_t total_uuids = sizeof(fq_name_uuids)/sizeof(fq_name_uuids[0]);

static tbb::mutex mutex_;
vector<string> updated;
vector<string> deleted;
set<string> updates;
set<string> deletes;

class CqlIfTest : public cass::cql::CqlIf {
public:
    CqlIfTest(EventManager *evm, const vector<string> &cassandra_ips,
              int cassandra_port, const string &cassandra_user,
              const string &cassandra_password) :
            cass::cql::CqlIf(evm, cassandra_ips, cassandra_port, cassandra_user,
                             cassandra_password) {
    }

    virtual bool Db_Init() { return true; }
    virtual void Db_Uninit() { }
    virtual bool Db_SetTablespace(const string &tablespace) { return true;}
    virtual bool Db_UseColumnfamily(const string &cfname) { return true; }
    virtual bool Db_GetRow(GenDb::ColList *out, const string &cfname,
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
        while (i < total_uuids && column_name >= fq_name_uuids[i])
            i++;
        if (i >= total_uuids)
            return true;

        for (int pushed = 0; i < total_uuids; i++) {
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
        const string &cfname,
        const vector<GenDb::DbDataValueVec> &v_rowkey,
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
    virtual vector<GenDb::Endpoint> Db_GetEndpoints() const {
        return vector<GenDb::Endpoint>();
    }
};

class ConfigCassandraClientMock : public ConfigCassandraClient {
public:
    ConfigCassandraClientMock(ConfigClientManager *mgr, EventManager *evm,
        const ConfigClientOptions &options,
        int num_workers) : ConfigCassandraClient(mgr, evm, options,
            num_workers) {
    }
    virtual bool BulkDataSync() {
        return ConfigCassandraClient::BulkDataSync();
    }

    void DisableWorkQueues(bool flag) {
        BOOST_FOREACH(ConfigCassandraPartition *partition, partitions()) 
            partition->obj_process_queue()->set_disable(flag);
    }

private:
    virtual void GenerateAndPushJson(
    const string &uuid_key, const string &obj_type,
    const CassColumnKVVec &cass_data_vec, bool add_change) {
        if (cass_data_vec.empty())
            return;
        tbb::mutex::scoped_lock lock(mutex_);
        if (add_change) {
            updated.push_back(uuid_key);
        } else {
            deleted.push_back(uuid_key);
        }
    }
    virtual uint32_t GetFQNameEntriesToRead() const { return 4; }
    virtual uint32_t GetNumReadRequestToBunch() const { return 4; }
    virtual const int GetMaxRequestsToYield() const { return 4; }
    virtual const uint64_t GetInitRetryTimeUSec() const { return 10; }
    virtual bool SkipTimeStampCheckForTypeAndFQName() const { return false; }
};

class ConfigJsonParserTest : public ConfigJsonParserBase {
    void SetupGraphFilter() {
        AddObjectType("virtual_network");
    }
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter,
                         bool add_change) {
        return true;
    }
    
};

class ConfigClientManagerTest : public ConfigClientManager {
public:
    ConfigClientManagerTest(EventManager *evm,
        string hostname, string module_name,
        const ConfigClientOptions& config_options) :
                ConfigClientManager(evm, hostname, module_name,
                                    config_options) {
    }
};


class ConfigCassandraClientReaderTest : public ::testing::Test {
protected:
    ConfigCassandraClientReaderTest() :
        thread_(&evm_),
        config_client_manager_(new ConfigClientManagerTest(&evm_,
            "localhost", "config-test", config_options_)) {
    }

    virtual void SetUp() {
        Clear();
        thread_.Start();
        task_util::WaitForIdle();

        config_client_manager_->Initialize();
        task_util::WaitForIdle();
        for (size_t i = 0; i < total_uuids; i++) {
            vector<string> tokens;
            boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
            updates.insert(tokens[4]);
        }
        TASK_UTIL_EXPECT_TRUE(IsConnectionUp());
        TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());
        Clear();

    }

    virtual void TearDown() {
        config_client_manager_->set_end_of_rib_computed(false);
        for (size_t i = 0; i < total_uuids; i++) {
            vector<string> tokens;
            boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
            config_client_manager_->config_amqp_client()->EnqueueUUIDRequest(
                "DELETE", "virtual_network", tokens[4]);
        }
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    bool IsAllExpectedEntriesProcessed() {
        tbb::mutex::scoped_lock lock(mutex_);
        if (updates.size() != updated.size())
            return false;
        if (deletes.size() != deleted.size())
            return false;

        sort(updated.begin(), updated.end());
        sort(deleted.begin(), deleted.end());

        if (!equal(updates.begin(), updates.end(), updated.begin()))
            return false;
        return equal(deletes.begin(), deletes.end(), deleted.begin());
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
    const ConfigClientOptions config_options_;
    boost::scoped_ptr<ConfigClientManagerTest> config_client_manager_;
};

class ConfigCassandraClientReaderTest1 : public ConfigCassandraClientReaderTest,
                                         public WithParamInterface<int> {
};

// For all 10 unique uuids, enqueue all combinations of updates and deletes
// together. (No duplicate operation for any uuid)
TEST_P(ConfigCassandraClientReaderTest1, Basic) {
    ConfigCassandraClientMock *mock = dynamic_cast<ConfigCassandraClientMock *>(
        config_client_manager_->config_db_client());
    int pattern = GetParam();
    mock->DisableWorkQueues(true);

    for (size_t i = 0; i < total_uuids; i++) {
        vector<string> tokens;
        boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
        string uuid = tokens[4];

        if (pattern & (1 << i)) {
            config_client_manager_->config_amqp_client()->
            EnqueueUUIDRequest("DELETE", "virtual_network", uuid);
            deletes.insert(uuid);
        } else {
            config_client_manager_->config_amqp_client()->
                EnqueueUUIDRequest("UPDATE", "virtual_network", uuid);
            updates.insert(uuid);
        }
    }

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(IsAllExpectedEntriesProcessed());
    mock->DisableWorkQueues(false);
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());
}

// update/delete bit-mask for entire uuids set
INSTANTIATE_TEST_CASE_P(ConfigCassandraClientReaderTest1WithParams,
    ConfigCassandraClientReaderTest1, Range<int>(0, (1 << total_uuids) - 1));

class ConfigCassandraClientReaderTest2 : public ConfigCassandraClientReaderTest,
                                         public WithParamInterface<int> {
};

// For all 10 unique uuids, enqueue multiple operations for each uuid.
// Each operations set (per uuid) would be based on a bit-mask of size 4 with
// bit value 0 as update and bit value 1 as delete. Last operation enqueued
// is the expected outcome. Duplicate operations for a given uuid are enqueued
// next to each other.
TEST_P(ConfigCassandraClientReaderTest2, Basic) {
    ConfigCassandraClientMock *mock = dynamic_cast<ConfigCassandraClientMock *>(
        config_client_manager_->config_db_client());
    int pattern = GetParam();
    mock->DisableWorkQueues(true);
    for (size_t i = 0; i < total_uuids; i++) {
        vector<string> tokens;
        boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
        string uuid = tokens[4];

        for (int j = 0; j < 4; j++) {
            if (pattern & (1 << j)) {
                config_client_manager_->config_amqp_client()->
                    EnqueueUUIDRequest("DELETE", "virtual_network", uuid);
                updates.erase(uuid);
                deletes.insert(uuid);
            } else {
                config_client_manager_->config_amqp_client()->
                    EnqueueUUIDRequest("UPDATE", "virtual_network", uuid);
                deletes.erase(uuid);
                updates.insert(uuid);
            }
        }
    }

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(IsAllExpectedEntriesProcessed());
    mock->DisableWorkQueues(false);
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());
}

// update/delete bit-mask of operations for each uuid.
INSTANTIATE_TEST_CASE_P(ConfigCassandraClientReaderTest2WithParams,
    ConfigCassandraClientReaderTest2, Range<int>(0, (1 << 4) - 1));

class ConfigCassandraClientReaderTest3 : public ConfigCassandraClientReaderTest,
                                         public WithParamInterface<int> {
};

// Same as previous test, except that duplicate operations for uuid are
// spread across different uuids.
//
// For all 10 unique uuids, enqueue multiple operations for each uuid.
// Each operations set (per uuid) would be based on a bit-mask of size 4 with
// bit value 0 as update and bit value 1 as delete. Last operation enqueued
// is the expected outcome. Duplicate operations for a given uuid are _not_
// enqueued next to each other.
TEST_P(ConfigCassandraClientReaderTest3, Basic) {
    ConfigCassandraClientMock *mock = dynamic_cast<ConfigCassandraClientMock *>(
        config_client_manager_->config_db_client());
    int pattern = GetParam();
    mock->DisableWorkQueues(true);
    for (int j = 0; j < 4; j++) {
        for (size_t i = 0; i < total_uuids; i++) {
            vector<string> tokens;
            boost::split(tokens, fq_name_uuids[i], boost::is_any_of(":"));
            string uuid = tokens[4];

            if (pattern & (1 << j)) {
                config_client_manager_->config_amqp_client()->
                    EnqueueUUIDRequest("DELETE", "virtual_network", uuid);
                updates.erase(uuid);
                deletes.insert(uuid);
            } else {
                config_client_manager_->config_amqp_client()->
                    EnqueueUUIDRequest("UPDATE", "virtual_network", uuid);
                deletes.erase(uuid);
                updates.insert(uuid);
            }
        }
    }

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(IsAllExpectedEntriesProcessed());
    mock->DisableWorkQueues(false);
    TASK_UTIL_EXPECT_TRUE(IsAllExpectedEntriesProcessed());
}

// update/delete bit-mask of operations for each uuid.
INSTANTIATE_TEST_CASE_P(ConfigCassandraClientReaderTest3WithParams,
    ConfigCassandraClientReaderTest3, Range<int>(0, (1 << 4) - 1));

int main(int argc, char **argv) {
    InitGoogleTest(&argc, argv);
    LoggingInit();
    ConfigAmqpClient::set_disable(true);
    ConfigFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientMock *>());
    ConfigFactory::Register<cass::cql::CqlIf>(boost::factory<CqlIfTest *>());
    ConfigFactory::Register<ConfigJsonParserBase>(
        boost::factory<ConfigJsonParserTest *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
