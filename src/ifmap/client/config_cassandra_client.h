/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass_client_h
#define ctrlplane_config_cass_client_h

#include "config_db_client.h"
#include "database/gendb_if.h"
#include "json_adapter_data.h"

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
    bool GetRow(const std::string &uuid);

private:
    void InitRetry();
    bool ParseCassandraResponse(const std::string &uuid,
                const GenDb::ColList &col_list, CassDataVec *cass_data_vec);
    void AddUuidEntry(const string &uuid);

    EventManager *evm_;
    GenDbIfPtr dbif_;
};

#endif // ctrlplane_config_cass_client_h
