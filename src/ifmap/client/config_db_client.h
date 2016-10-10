/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_db_client_h
#define ctrlplane_config_db_client_h

#include <tbb/atomic.h>

#include <string>
#include <vector>

struct IFMapConfigOptions;

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
    void set_end_of_rib_computed(bool value) {
        end_of_rib_computed_ = value;
    }
    bool end_of_rib_computed() const { return end_of_rib_computed_; }

private:
    std::string config_db_user_;
    std::string config_db_password_;
    std::vector<std::string> config_db_ips_;
    std::vector<int> config_db_ports_;
    tbb::atomic<bool> end_of_rib_computed_;
};

#endif // ctrlplane_config_db_client_h
