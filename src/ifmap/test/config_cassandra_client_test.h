/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ctrlplane_config_cassandra_client_test_h
#define ctrlplane_config_cassandra_client_test_h

#include <boost/foreach.hpp>
#include <fstream>
#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_cass2json_adapter.h"
#include "ifmap/client/config_cassandra_client.h"
#include "ifmap/client/config_client_manager.h"
#include "ifmap/client/config_json_parser.h"

class ConfigCassandraClientTest : public ConfigCassandraClient {
public:
    ConfigCassandraClientTest(ConfigClientManager *mgr, EventManager *evm,
        const IFMapConfigOptions &options, ConfigJsonParser *in_parser,
        int num_workers) : ConfigCassandraClient(mgr, evm, options, in_parser,
            num_workers), db_index_(num_workers), cevent_(0) {
    }

    virtual void HandleObjectDelete(const std::string &uuid) {
        std::vector<std::string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        std::string u;
        if (tokens.size() == 2) {
            u = tokens[1];
        } else {
            u = uuid;
        }
        ConfigCassandraClient::HandleObjectDelete(u);
    }

    virtual void AddFQNameCache(const std::string &uuid,
                                const std::string &obj_type,
                                const std::string &obj_name) {
        std::vector<std::string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        ConfigCassandraClient::AddFQNameCache(tokens[1], obj_type, obj_name);
    }

    virtual void InvalidateFQNameCache(const std::string &uuid) {
        vector<string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        ConfigCassandraClient::InvalidateFQNameCache(tokens[1]);
    }

    virtual int HashUUID(const std::string &uuid) const {
        std::string u = uuid;
        size_t from_front_pos = uuid.find(':');
        if (from_front_pos != std::string::npos)  {
            u = uuid.substr(from_front_pos+1);
        }
        return ConfigCassandraClient::HashUUID(u);
    }

    virtual bool ReadObjUUIDTable(std::set<std::string> *uuid_list) {
        BOOST_FOREACH(std::string uuid_key, *uuid_list) {
            vector<string> tokens;
            boost::split(tokens, uuid_key, boost::is_any_of(":"));
            int index = atoi(tokens[0].c_str());
            std::string u = tokens[1];
            assert((*events())[index].IsObject());
            int idx = HashUUID(u);
            db_index_[idx].insert(make_pair(u, index));
            ProcessObjUUIDTableEntry(u, GenDb::ColList());
        }
        return true;
    }

    void ParseObjUUIDTableEntry(const std::string &uuid,
            const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec,
            ConfigCassandraParseContext &context) {
        // Retrieve event index prepended to uuid, to get to the correct db.
        int idx = HashUUID(uuid);
        UUIDIndexMap::iterator it = db_index_[idx].find(uuid);
        int index = it->second;

        if (!(*events())[contrail_rapidjson::SizeType(index)]["db"].HasMember(
                    uuid.c_str()))
            return;
        for (contrail_rapidjson::Value::ConstMemberIterator k =
             (*events())[contrail_rapidjson::SizeType(index)]["db"]
                [uuid.c_str()].MemberBegin();
             k != (*events())[contrail_rapidjson::SizeType(index)]["db"]
                [uuid.c_str()].MemberEnd();
             ++k) {
            const char *k1 = k->name.GetString();
            const char *v1;
            if (k->value.IsArray())
                v1 = k->value[contrail_rapidjson::SizeType(0)].GetString();
            else
                v1 = k->value.GetString();
            ParseObjUUIDTableEachColumnBuildContext(uuid, k1, v1, 0,
                                                    cass_data_vec, context);
        }
        db_index_[idx].erase(it);
    }

    std::string GetUUID(const std::string &key) const {
        size_t temp = key.rfind(':');
        return (temp == std::string::npos) ? key : key.substr(temp+1);
    }

    std::string FetchUUIDFromFQNameEntry(const std::string &key) const {
        size_t temp = key.rfind(':');
        return (temp == std::string::npos) ?
            "" : (boost::lexical_cast<std::string>(cevent_-1) + ":" +
                    key.substr(temp+1));
    }

    bool BulkDataSync() {
        ConfigCassandraClient::ObjTypeUUIDList uuid_list;
        for (contrail_rapidjson::Value::ConstMemberIterator k =
             (*events())[contrail_rapidjson::SizeType(cevent_-1)]
                ["OBJ_FQ_NAME_TABLE"].MemberBegin();
             k != (*events())[contrail_rapidjson::SizeType(cevent_-1)]
                ["OBJ_FQ_NAME_TABLE"].  MemberEnd(); ++k) {
            std::string obj_type = k->name.GetString();
            for (contrail_rapidjson::Value::ConstMemberIterator l =
                    (*events())[contrail_rapidjson::SizeType(cevent_-1)]
                        ["OBJ_FQ_NAME_TABLE"][obj_type.c_str()].MemberBegin();
                 l != (*events())[contrail_rapidjson::SizeType(cevent_-1)]
                    ["OBJ_FQ_NAME_TABLE"][obj_type.c_str()].MemberEnd(); l++) {
                UpdateFQNameCache(l->name.GetString(), obj_type, uuid_list);
            }
        }
        return EnqueueDBSyncRequest(uuid_list);
    }

    contrail_rapidjson::Document *events() {
        if (db_load_.IsArray())
            return &db_load_;
        return &events_;
    }
    contrail_rapidjson::Document *db_load() { return &db_load_; }
    size_t *cevent() { return &cevent_; }

    static void ParseEventsJson(
            ConfigClientManager *config_client_manager,
            std::string events_file) {
        ConfigCassandraClientTest *config_cassandra_client =
            dynamic_cast<ConfigCassandraClientTest *>(
                    config_client_manager->config_db_client());
        std::string json_message = FileRead(events_file);
        assert(json_message.size() != 0);
        contrail_rapidjson::Document *doc = config_cassandra_client->events();
        doc->Parse<0>(json_message.c_str());
        if (doc->HasParseError()) {
            size_t pos = doc->GetErrorOffset();
            // GetParseError returns const char *
            std::cout << "Error in parsing JSON message from rabbitMQ at "
                << pos << "with error description"
                << doc->GetParseError()
                << std::endl;
            exit(-1);
        }

        if (doc->IsObject() && doc->HasMember("cassandra") &&
                (*doc)["cassandra"].HasMember("config_db_uuid")) {
            contrail_rapidjson::Document *db_load =
                config_cassandra_client->db_load();
            contrail_rapidjson::Document::AllocatorType &a =
                db_load->GetAllocator();
            db_load->SetArray();
            contrail_rapidjson::Value v;
            db_load->PushBack(v.SetObject(), a);
            (*db_load)[0].AddMember("operation", "db_sync", a);
            (*db_load)[0].AddMember("OBJ_FQ_NAME_TABLE",
                (*doc)["cassandra"]["config_db_uuid"]["obj_fq_name_table"], a);
            (*db_load)[0].AddMember("db",
                (*doc)["cassandra"]["config_db_uuid"]["obj_uuid_table"], a);
        } else if (doc->IsObject() && doc->HasMember("cassandra") &&
                (*doc)["cassandra"].HasMember("obj_fq_name_table")) {
            contrail_rapidjson::Document *db_load =
                config_cassandra_client->db_load();
            contrail_rapidjson::Document::AllocatorType &a =
                db_load->GetAllocator();
            db_load->SetArray();
            contrail_rapidjson::Value v;
            db_load->PushBack(v.SetObject(), a);
            (*db_load)[0].AddMember("operation", "db_sync", a);
            (*db_load)[0].AddMember("OBJ_FQ_NAME_TABLE",
                (*doc)["cassandra"]["obj_fq_name_table"], a);
            (*db_load)[0].AddMember("db",
                (*doc)["cassandra"]["obj_uuid_table"], a);
        }
    }

    static void FeedEventsJson(ConfigClientManager *config_client_manager) {
        ConfigCassandraClientTest *config_cassandra_client =
            dynamic_cast<ConfigCassandraClientTest *>(
                    config_client_manager->config_db_client());
        contrail_rapidjson::Document *events =
            config_cassandra_client->events();
        while ((*config_cassandra_client->cevent())++ < events->Size()) {
            size_t cevent = *config_cassandra_client->cevent() - 1;
            if ((*events)[contrail_rapidjson::SizeType(cevent)]["operation"]
                    .GetString() == std::string("pause")) {
                break;
            }

            if ((*events)[contrail_rapidjson::SizeType(cevent)]["operation"]
                    .GetString() == std::string("db_sync")) {
                config_cassandra_client->BulkDataSync();
                continue;
            }

            config_client_manager->config_amqp_client()->ProcessMessage(
                (*events)[contrail_rapidjson::SizeType(cevent)]["message"]
                    .GetString());
        }
        task_util::WaitForIdle();
    }

    static std::string FileRead(const std::string &filename) {
        std::ifstream file(filename.c_str());
        std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
        return content;
    }

private:
    typedef std::map<std::string, int> UUIDIndexMap;
    std::vector<UUIDIndexMap> db_index_;
    contrail_rapidjson::Document events_;
    contrail_rapidjson::Document db_load_;
    size_t cevent_;
};

#endif // ctrlplane_config_cassandra_client_test_h
