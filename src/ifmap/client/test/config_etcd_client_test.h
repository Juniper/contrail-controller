/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ctrlplane_config_etcd_client_test_h
#define ctrlplane_config_etcd_client_test_h

#include <boost/foreach.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "config-client-mgr/config_cass2json_adapter.h"
#include "config-client-mgr/config_client_manager.h"
#include "config-client-mgr/config_etcd_client.h"
#include "config-client-mgr/config_factory.h"
#include "database/etcd/eql_if.h"
#include "ifmap/client/config_json_parser.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using namespace std;
using etcd::etcdql::EtcdIf;
using etcd::etcdql::EtcdResponse;
using etcd::etcdql::WatchAction;
using contrail_rapidjson::StringBuffer;
using contrail_rapidjson::Writer;

static int num_bunch = 8;
static int max_yield = 4;
static Document dbDoc_;
static int db_index = 2;

class EqlIfTest : public EtcdIf {
public:
    EqlIfTest(const vector<string> &etcd_hosts,
              const int port,
              bool useSsl) : EtcdIf(etcd_hosts,
                                   port,
                                   useSsl) {
    }

    virtual bool Connect() { return true; }

    virtual EtcdResponse Get(const string& key,
                             const string& range_end,
                             int limit) {
        EtcdResponse resp;
        multimap<string, string> kvs;

        if (--db_index == 0) {
            resp.set_err_code(-1);
            return resp;
        }

        /**
          * Make sure the database document is populated.
          * Set error code to 0
          */
        resp.set_err_code(0);

        for (Value::ConstMemberIterator itr = dbDoc_.MemberBegin();
             itr != dbDoc_.MemberEnd(); itr++) {
            /**
              * Get the uuid string and the value from the
              * database Document created from the input file.
              */
            string uuid_str = itr->name.GetString();
            StringBuffer sb;
            Writer<StringBuffer> writer(sb);
            itr->value.Accept(writer);
            string value_str = sb.GetString();

            /**
              * Populate the key-value store to be saved
              * in the EtcdResponse.
              */
            kvs.insert(pair<string, string> (uuid_str, value_str));
        }

        /**
          * Save the kvs in the EtcdResponse and return
          * to caller.
          */
        resp.set_kv_map(kvs);

        return resp;
    }

    static void ParseDatabase(string db_file) {
        string json_db = FileRead(db_file);
        assert(json_db.size() != 0);

        Document *dbDoc = &dbDoc_;
        dbDoc->Parse<0>(json_db.c_str());
        if (dbDoc->HasParseError()) {
            size_t pos = dbDoc->GetErrorOffset();
            // GetParseError returns const char *
            std::cout << "Error in parsing JSON DB at "
                << pos << "with error description"
                << dbDoc->GetParseError()
                << std::endl;
            exit(-1);
        }
        task_util::WaitForIdle();
    }

    static string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }
};

class ConfigEtcdClientTest : public ConfigEtcdClient {
public:
    ConfigEtcdClientTest(
             ConfigClientManager *mgr,
             EventManager *evm,
             const ConfigClientOptions &options,
             int num_workers) :
                   ConfigEtcdClient(mgr,
                                    evm,
                                    options,
                                    num_workers),
                   cevent_(0) {
    }

    Document *ev_load() { return &evDoc_; }

    string GetJsonValue(const string &uuid) {
        if (!evDoc_.HasMember(uuid.c_str())) {
            return string();
        }
        Value &val = evDoc_[uuid.c_str()];
        StringBuffer sb;
        Writer<StringBuffer> writer(sb);
        val.Accept(writer);
        string value_str = sb.GetString();
        return (value_str);
    }

    void ParseEventsJson(string events_file) {
        string json_events = FileRead(events_file);
        assert(json_events.size() != 0);

        Document *eventDoc = &evDoc_;
        eventDoc->Parse<0>(json_events.c_str());
        if (eventDoc->HasParseError()) {
            size_t pos = eventDoc->GetErrorOffset();
            // GetParseError returns const char *
            std::cout << "Error in parsing JSON events at "
                << pos << "with error description"
                << eventDoc->GetParseError()
                << std::endl;
            exit(-1);
        }
    }

    void FeedEventsJson() {
        EtcdResponse resp;
        resp.set_err_code(0);

        Document *eventDoc = &evDoc_;

        size_t curr = 0;
        Value::ConstMemberIterator itr = eventDoc->MemberBegin();
        while (curr++ < cevent_) {
             itr++;
        }

        while (itr != eventDoc->MemberEnd()) {
            cevent_++;
            /**
              * Get the uuid string and the value from the
              * database Document created from the input file.
              */
            string uuid_str = itr->name.GetString();
            StringBuffer sb;
            Writer<StringBuffer> writer(sb);
            itr->value.Accept(writer);
            string value_str = sb.GetString();
            string action = uuid_str.substr(1, 6);
            if (action == "CREATE") {
                resp.set_action(WatchAction(0));
            } else if (action == "UPDATE") {
                resp.set_action(WatchAction(1));
            } else if (action == "DELETE") {
                resp.set_action(WatchAction(2));
            } else if (action == "PAUSED") {
                break;
            }
            assert((resp.action() >= WatchAction(0)) &&
                   (resp.action() <= (WatchAction(2))));
            resp.set_key(uuid_str);
            resp.set_val(value_str);
            ConfigEtcdClient::ProcessResponse(resp);
            itr++;
        }
        task_util::WaitForIdle();
    }

    static string FileRead(const string &filename) {
        return (EqlIfTest::FileRead(filename));
    }

private:
    virtual uint32_t GetNumUUIDRequestToBunch() const {
        return num_bunch;
    }

    virtual const int GetMaxRequestsToYield() const {
        return max_yield;
    }

    Document evDoc_;
    size_t cevent_;
};

class ConfigClientManagerMock : public ConfigClientManager {
public:
    ConfigClientManagerMock(
                   EventManager *evm,
                   string hostname,
                   string module_name,
                   const ConfigClientOptions& config_options) :
             ConfigClientManager(evm,
                                 hostname,
                                 module_name,
                                 config_options) {
    }
};
#endif
