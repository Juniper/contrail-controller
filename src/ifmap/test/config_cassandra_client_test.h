/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ctrlplane_config_cassandra_client_test_h
#define ctrlplane_config_cassandra_client_test_h

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

    virtual void HandleObjectDelete(const std::string &type,
                                    const std::string &uuid) {
        std::vector<std::string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        std::string u = tokens[1];
        ConfigCassandraClient::HandleObjectDelete(type, u);
    }

    virtual void AddFQNameCache(const std::string &uuid,
                                const std::string &obj_name) {
        std::vector<std::string> tokens;
        boost::split(tokens, uuid, boost::is_any_of(":"));
        ConfigCassandraClient::AddFQNameCache(tokens[1], obj_name);
    }

    virtual int HashUUID(const std::string &uuid) const {
        std::string u = uuid;
        size_t from_front_pos = uuid.find(':');
        if (from_front_pos != std::string::npos)  {
            u = uuid.substr(from_front_pos+1);
        }
        return ConfigCassandraClient::HashUUID(u);
    }

    virtual bool ReadUuidTableRow(const std::string &obj_type,
                                  const std::string &uuid_key) {
        std::vector<std::string> tokens;
        boost::split(tokens, uuid_key, boost::is_any_of(":"));
        int index = atoi(tokens[0].c_str());
        std::string u = tokens[1];
        assert(events_[index].IsObject());
        int idx = HashUUID(u);
        db_index_[idx].insert(make_pair(u, index));
        return ParseRowAndEnqueueToParser(obj_type, u, GenDb::ColList());
    }

    bool ParseUuidTableRowResponse(const std::string &uuid,
            const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec,
            ConfigCassandraParseContext &context) {
        // Retrieve event index prepended to uuid, to get to the correct db.
        int idx = HashUUID(uuid);
        UUIDIndexMap::iterator it = db_index_[idx].find(uuid);
        int index = it->second;

        if (!events_[contrail_rapidjson::SizeType(index)]["db"].HasMember(
                    uuid.c_str()))
            return true;
        for (contrail_rapidjson::Value::ConstMemberIterator k =
             events_[contrail_rapidjson::SizeType(index)]["db"]
                [uuid.c_str()].MemberBegin();
             k != events_[contrail_rapidjson::SizeType(index)]["db"]
                [uuid.c_str()].MemberEnd();
             ++k) {
            const char *k1 = k->name.GetString();
            const char *v1;
            if (k->value.IsArray())
                v1 = k->value[contrail_rapidjson::SizeType(0)].GetString();
            else
                v1 = k->value.GetString();
            ParseUuidTableRowJson(uuid, k1, v1, 0, cass_data_vec, context);
        }
        db_index_[idx].erase(it);
        return true;
    }

    std::string GetUUID(const std::string &key, const std::string &obj_type) {
        size_t temp = key.rfind(':');
        return (temp == std::string::npos) ?
            "" : (boost::lexical_cast<std::string>(cevent_-1) + ":" +
                    key.substr(temp+1));
    }

    bool BulkDataSync() {
        ConfigCassandraClient::ObjTypeUUIDList uuid_list;
        for (contrail_rapidjson::Value::ConstMemberIterator k =
             events_[contrail_rapidjson::SizeType(cevent_-1)]
                ["OBJ_FQ_NAME_TABLE"].MemberBegin();
             k != events_[contrail_rapidjson::SizeType(cevent_-1)]
                ["OBJ_FQ_NAME_TABLE"].  MemberEnd(); ++k) {
            std::string obj_type = k->name.GetString();
            for (contrail_rapidjson::Value::ConstMemberIterator l =
                    events_[contrail_rapidjson::SizeType(cevent_-1)]
                        ["OBJ_FQ_NAME_TABLE"][obj_type.c_str()].MemberBegin();
                 l != events_[contrail_rapidjson::SizeType(cevent_-1)]
                    ["OBJ_FQ_NAME_TABLE"][obj_type.c_str()].MemberEnd(); l++) {
                UpdateCache(l->name.GetString(), obj_type, uuid_list);
            }
        }
        return EnqueueUUIDRequest(uuid_list);
    }

    contrail_rapidjson::Document *events() { return &events_; }
    size_t *cevent() { return &cevent_; }

private:
    typedef std::map<std::string, int> UUIDIndexMap;
    std::vector<UUIDIndexMap> db_index_;
    contrail_rapidjson::Document events_;
    size_t cevent_;
};

#endif // ctrlplane_config_cassandra_client_test_h
