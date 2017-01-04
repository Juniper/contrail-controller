/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_client_manager_h
#define ctrlplane_config_client_manager_h

#include <boost/scoped_ptr.hpp>

class ConfigAmqpClient;
class ConfigDbClient;
class ConfigJsonParser;
class EventManager;
class IFMapServer;
struct IFMapConfigOptions;

/*
 * This class is the manager that over-sees the retrieval of user configuration.
 * It interacts with the rabbit-mq client, the database-client and the parser
 * that parses the configuration received from the database-client. Its the
 * coordinator between these 3 pieces.
 */
class ConfigClientManager {
public:
    ConfigClientManager(EventManager *evm, IFMapServer *ifmap_server,
                        std::string hostname,
                        const IFMapConfigOptions& config_options);
    void Initialize();
    ConfigAmqpClient *config_amqp_client() const;
    ConfigDbClient *config_db_client() const;
    ConfigJsonParser *config_json_parser() const;
    bool GetEndOfRibComputed() const;
    void EnqueueUUIDRequest(std::string uuid_str, std::string obj_type,
                            std::string oper);

private:
    EventManager *evm_;
    IFMapServer *ifmap_server_;
    boost::scoped_ptr<ConfigJsonParser> config_json_parser_;
    boost::scoped_ptr<ConfigDbClient> config_db_client_;
    boost::scoped_ptr<ConfigAmqpClient> config_amqp_client_;
    int thread_count_;
};

#endif // ctrlplane_config_client_manager_h
