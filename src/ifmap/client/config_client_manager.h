/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_client_manager_h
#define ctrlplane_config_client_manager_h

#include <boost/scoped_ptr.hpp>

class ConfigDbClient;
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
                        const IFMapConfigOptions& config_options);
    void Initialize();
    ConfigDbClient *config_db_client() const;
    bool GetEndOfRibComputed() const;

private:
    static int thread_count_;

    EventManager *evm_;
    IFMapServer *ifmap_server_;
    boost::scoped_ptr<ConfigDbClient> config_db_client_;
};

#endif // ctrlplane_config_client_manager_h
