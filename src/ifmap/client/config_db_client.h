/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_db_client_h
#define ctrlplane_config_db_client_h

#include <string>
#include <vector>

#include "config_client_manager.h"
#include "ifmap/ifmap_table.h"

struct IFMapConfigOptions;
struct ConfigDBConnInfo;
struct ConfigDBFQNameCacheEntry;
struct ConfigDBUUIDCacheEntry;
/*
 * This is the base class for interactions with a database that stores the user
 * configuration.
 */
class ConfigDbClient {
public:
    ConfigDbClient(const IFMapConfigOptions &options);
    virtual ~ConfigDbClient();
    std::string config_db_user() const;
    std::string config_db_password() const;
    std::vector<std::string> config_db_ips() const;
    int GetFirstConfigDbPort() const;
    virtual void InitDatabase() = 0;
    virtual void EnqueueUUIDRequest(std::string uuid_str, std::string obj_type,
                                    std::string oper) = 0;
    virtual void FormDeleteRequestList(const std::string &uuid,
                              ConfigClientManager::RequestList *req_list,
                              IFMapTable::RequestKey *key, bool add_change) = 0;

    virtual void AddFQNameCache(const std::string &uuid,
                   const std::string &obj_type, const std::string &fq_name) = 0;
    virtual void InvalidateFQNameCache(const std::string &uuid) = 0;
    virtual bool UUIDToFQNameShow(const std::string &uuid,
                                  ConfigDBFQNameCacheEntry &entry) const = 0;
    virtual bool UUIDToFQNameShow(const std::string &start_uuid,
                      uint32_t num_entries,
                      std::vector<ConfigDBFQNameCacheEntry> &entries) const = 0;

    virtual void GetConnectionInfo(ConfigDBConnInfo &status) const = 0;

    virtual bool UUIDToObjCacheShow(int inst_num, const std::string &uuid,
                                  ConfigDBUUIDCacheEntry &entry) const = 0;
    virtual bool UUIDToObjCacheShow(int inst_num, const std::string &start_uuid,
                      uint32_t num_entries,
                      std::vector<ConfigDBUUIDCacheEntry> &entries) const = 0;

private:
    std::string config_db_user_;
    std::string config_db_password_;
    std::vector<std::string> config_db_ips_;
    std::vector<int> config_db_ports_;
};

#endif // ctrlplane_config_db_client_h
