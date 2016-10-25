/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_cass_config_client_h
#define ctrlplane_cass_config_client_h

#include "config_db_client.h"
#include "database/gendb_if.h"

class EventManager;

/*
 * This class has the functionality to interact with the cassandra servers that
 * store the user configuration.
 */
class ConfigCassandraClient : public ConfigDbClient {
public:
    static const std::string kUuidTableName;
    static const std::string kFqnTableName;
    static const std::string kCassClientTaskId;
    // wait time before retrying in seconds
    static const int kInitRetryTimeSec = 5;

    typedef boost::scoped_ptr<GenDb::GenDbIf> GenDbIfPtr;

    ConfigCassandraClient(EventManager *evm, const IFMapConfigOptions &options);
    virtual ~ConfigCassandraClient();
    virtual void InitDatabase();

private:
    void InitRetry();

    EventManager *evm_;
    GenDbIfPtr dbif_;
};

#endif // ctrlplane_cass_config_client_h
