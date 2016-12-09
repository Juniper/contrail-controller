/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_cass_client_h
#define ctrlplane_config_cass_client_h

#include "config_db_client.h"
#include "config_json_parser.h"
#include "database/gendb_if.h"
#include "json_adapter_data.h"

class EventManager;
class ConfigJsonParser;

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

    ConfigCassandraClient(EventManager *evm, const IFMapConfigOptions &options,
                          ConfigJsonParser *in_parser);
    virtual ~ConfigCassandraClient();
    virtual void InitDatabase();
    bool ReadUuidTableRow(const std::string &uuid);

private:
    void InitRetry();
    bool ParseUuidTableRowResponse(const std::string &uuid,
                const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec);
    void AddUuidEntry(const string &uuid);
    bool BulkDataSync();
    bool ReadAllUuidTableRows();
    bool ParseRowAndEnqueueToParser(const string &uuid_key,
                                    const GenDb::ColList &col_list);

    EventManager *evm_;
    GenDbIfPtr dbif_;
    ConfigJsonParser *parser_;
};

#endif // ctrlplane_config_cass_client_h
