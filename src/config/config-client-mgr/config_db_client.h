/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef config_db_client_h
#define config_db_client_h

#include <string>
#include <vector>

#include "config_client_manager.h"

struct ConfigClientOptions;
struct ConfigDBConnInfo;
struct ConfigDBFQNameCacheEntry;
struct ConfigDBUUIDCacheEntry;
/*
 * This is the base class for interactions with a database that stores the user
 * configuration.
 */
class ConfigDbClient {
public:
    ConfigDbClient(const ConfigClientOptions &options);
    virtual ~ConfigDbClient();
    std::string config_db_user() const;
    std::string config_db_password() const;
    std::vector<std::string> config_db_ips() const;
    int GetFirstConfigDbPort() const;
    virtual void PostShutdown() = 0;
    virtual void InitDatabase() = 0;
    virtual void EnqueueUUIDRequest(std::string uuid_str, std::string obj_type,
                                    std::string oper) = 0;

    virtual void AddFQNameCache(const std::string &uuid,
                   const std::string &obj_type, const std::string &fq_name) = 0;
    virtual std::string FindFQName(const std::string &uuid) const = 0;
    virtual void InvalidateFQNameCache(const std::string &uuid) = 0;

    virtual bool UUIDToFQNameShow(
        const std::string &search_string, const std::string &last_uuid,
        uint32_t num_entries,
        std::vector<ConfigDBFQNameCacheEntry> *entries) const = 0;

    virtual void GetConnectionInfo(ConfigDBConnInfo &status) const = 0;

    virtual bool UUIDToObjCacheShow(
        const std::string &search_string, int inst_num,
        const std::string &last_uuid, uint32_t num_entries,
        std::vector<ConfigDBUUIDCacheEntry> *entries) const = 0;

    virtual bool IsListOrMapPropEmpty(const std::string &uuid_key, 
                                   const std::string &lookup_key) = 0;
private:
    std::string config_db_user_;
    std::string config_db_password_;
    std::vector<std::string> config_db_ips_;
    std::vector<int> config_db_ports_;
};

#endif  // config_db_client_h
