/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef config_client_manager_h
#define config_client_manager_h

#include <set>
#include <map>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp> 

#include <tbb/compat/condition_variable>
#include <tbb/mutex.h>

#include "config_client_options.h"
#include <rapidjson/document.h>

struct AutogenProperty;
class ConfigAmqpClient;
class ConfigDbClient;
struct DBRequest;
class EventManager;
class TaskTrigger;
class ConfigJsonParserBase;

/*
 * This class is the manager that over-sees the retrieval of user configuration.
 * It interacts with the rabbit-mq client, the database-client and the parser
 * that parses the configuration received from the database-client. Its the
 * coordinator between these 3 pieces.
 */
class ConfigClientManager {
public:
    static const int kNumConfigReaderTasks = 8;
    static const std::set<std::string> skip_properties;

    typedef std::set<std::string> ObjectTypeList;

    ConfigClientManager(EventManager *evm, std::string hostname, 
                        std::string module_name,
                        const ConfigClientOptions& config_options,
                        ConfigJsonParserBase *json_parser);

    virtual ~ConfigClientManager();

    virtual void Initialize();
    ConfigAmqpClient *config_amqp_client() const;
    ConfigDbClient *config_db_client() const;
    bool GetEndOfRibComputed() const;
    void EnqueueUUIDRequest(std::string oper, std::string obj_type,
                            std::string uuid_str);
    
    ConfigAmqpClient *config_amqp_client() { return config_amqp_client_.get(); }
    ConfigDbClient *config_db_client() { return config_db_client_.get(); }
    ConfigJsonParserBase *config_client_parser() { return config_client_parser_; }
    static int GetNumConfigReader();

    static int GetNumWorkers() {
        // AMQP reader and ConfigDB readers
        return GetNumConfigReader() + 1;
    }

    void EndOfConfig();
    void WaitForEndOfConfig();
    const ObjectTypeList &ObjectTypeListToRead() const {
        return obj_type_to_read_;
    }

    // Generation number identifies the current version of the config
    // available with ifmap server. While enqueuing the DB request to create
    // ifmap entries(node/link), config client manager uses the current
    // generation number to tag the config version in each ifmap config db entru
    uint64_t GetGenerationNumber() const {
        return generation_number_;
    }

    // Increment the generation number on reinit trigger
    // IFMap server identifies stale config db entries based on this generation
    // number. Any entry(node/link) which has generation number less than the
    // current generation number will be cleaned with "stale entry cleanup"
    // background task
    uint64_t IncrementGenerationNumber() {
        return generation_number_++;
    }

    // Reinit trigger with update of config parameter
    void ReinitConfigClient(const ConfigClientOptions &config);

    // Reinit trigger without update of config parameter.
    // Either from introspect or to force re-reading of cassandra
    void ReinitConfigClient();

    // This task trigger handles with init and reinit of config client manager
    bool InitConfigClient();

    bool is_reinit_triggered() {
        return reinit_triggered_;
    }
    static bool end_of_rib_computed() { return end_of_rib_computed_; }
    static void set_end_of_rib_computed(bool end_of_rib_computed) {
        end_of_rib_computed_ = end_of_rib_computed;
    }
    ConfigJsonParserBase *config_client_json_parser(){
        return config_client_json_parser_;
    }
private:
    typedef std::pair<std::string, std::string> LinkMemberPair;
    typedef std::pair<std::string, bool> LinkDataPair;
    typedef std::map<LinkMemberPair, std::string> ParentNameMap;
    typedef std::map<LinkMemberPair, LinkDataPair> LinkNameMap;
    typedef std::map<std::string, std::string> WrapperFieldMap;

    void SetUp();
    void PostShutdown();
    void SetDefaultSchedulingPolicy();

    LinkNameMap link_name_map_;
    ParentNameMap parent_name_map_;
    EventManager *evm_;
    ConfigJsonParserBase *config_client_parser_;
    boost::scoped_ptr<ConfigDbClient> config_db_client_;
    boost::scoped_ptr<ConfigAmqpClient> config_amqp_client_;
    int thread_count_;
    WrapperFieldMap wrapper_field_map_;
    ObjectTypeList obj_type_to_read_;
    uint64_t generation_number_;

    mutable tbb::mutex end_of_rib_sync_mutex_;
    tbb::interface5::condition_variable cond_var_;
    uint64_t end_of_rib_computed_at_;
    std::string hostname_;
    std::string module_name_;
    ConfigClientOptions config_options_;
    tbb::atomic<bool> reinit_triggered_;
    boost::shared_ptr<TaskTrigger> init_trigger_;
    static bool end_of_rib_computed_;
    ConfigJsonParserBase *config_client_json_parser_;
};

#endif // ctrlplane_config_client_manager_h
