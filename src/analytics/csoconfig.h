/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __CSO_CONFIG_H__
#define __CSO_CONFIG_H__


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "discovery/client/discovery_client.h"
#include "http/client/vncapi.h"
#include "parser_util.h"
#include "configdb_connection.h"

class Options;

class CsoHostnameRecord {
    public:
        explicit CsoHostnameRecord(std::string name, std::string cso_hostaddr, std::string cso_tenant,
        std::string cso_location, std::string cso_device):
            refreshed_(true), name_(name), cso_hostaddr_(cso_hostaddr), cso_tenant_(cso_tenant),
            cso_location_(cso_location), cso_device_(cso_device) {
        }
        bool operator==(const CsoHostnameRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.cso_hostaddr() == cso_hostaddr_ &&
            rhs.cso_tenant() == cso_tenant_ && rhs.cso_location() == cso_location_ &&
            rhs.cso_device() == cso_device_;
        }

        const std::string name() { return name_; }
        const std::string cso_hostaddr() const { return cso_hostaddr_; }
        const std::string cso_tenant() const { return cso_tenant_; }
        const std::string cso_location() const { return cso_location_; }
        const std::string cso_device() const { return cso_device_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string cso_hostaddr,
                     std::string cso_tenant, std::string cso_location,
                     std::string cso_device) {
             if (cso_hostaddr_ != cso_hostaddr)
                cso_hostaddr_ = cso_hostaddr;
             if (cso_tenant_ != cso_tenant)
                cso_tenant_ = cso_tenant;
             if (cso_location_ != cso_location)
                cso_location_ = cso_location;
             if (cso_device_ != cso_device)
                cso_device_ = cso_device;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            cso_hostaddr_;
        std::string            cso_tenant_;
        std::string            cso_location_;
        std::string            cso_device_;
};

class CsoApplicationRecord {
    public:
        explicit CsoApplicationRecord(std::string name, std::string cso_app_category,
        std::string cso_app_subcategory, std::string cso_default_app_groups, std::string cso_app_risk):
            refreshed_(true), name_(name), cso_app_category_(cso_app_category),
            cso_app_subcategory_(cso_app_subcategory), cso_default_app_groups_(cso_default_app_groups),
            cso_app_risk_(cso_app_risk) {
        }
        bool operator==(const CsoApplicationRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.cso_app_category() == cso_app_category_ &&
            rhs.cso_app_subcategory() == cso_app_subcategory_ &&
            rhs.cso_default_app_groups() == cso_default_app_groups_ &&
            rhs.cso_app_risk() == cso_app_risk_;
        }

        const std::string name() { return name_; }
        const std::string cso_app_category() const { return cso_app_category_; }
        const std::string cso_app_subcategory() const { return cso_app_subcategory_; }
        const std::string cso_default_app_groups() const { return cso_default_app_groups_; }
        const std::string cso_app_risk() const { return cso_app_risk_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string cso_app_category,
                     std::string cso_app_subcategory, std::string cso_default_app_groups,
                     std::string cso_app_risk) {
             if (cso_app_category_ != cso_app_category)
                cso_app_category_ = cso_app_category;
             if (cso_app_subcategory_ != cso_app_subcategory)
                cso_app_subcategory_ = cso_app_subcategory;
             if (cso_default_app_groups_ != cso_default_app_groups)
                cso_default_app_groups_ = cso_default_app_groups;
             if (cso_app_risk_ != cso_app_risk)
                cso_app_risk_ = cso_app_risk;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            cso_app_category_;
        std::string            cso_app_subcategory_;
        std::string            cso_default_app_groups_;
        std::string            cso_app_risk_;
};

class CsoTenantApplicationRecord {
    public:
        explicit CsoTenantApplicationRecord(std::string name, std::string cso_tenant_app_category,
        std::string cso_tenant_app_subcategory, std::string cso_tenant_app_groups,
        std::string cso_tenant_app_risk, std::string cso_tenant_app_service_tags):
            refreshed_(true), name_(name), cso_tenant_app_category_(cso_tenant_app_category),
            cso_tenant_app_subcategory_(cso_tenant_app_subcategory),
            cso_tenant_app_groups_(cso_tenant_app_groups), cso_tenant_app_risk_(cso_tenant_app_risk),
            cso_tenant_app_service_tags_(cso_tenant_app_service_tags) {
        }
        bool operator==(const CsoTenantApplicationRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.cso_tenant_app_category() == cso_tenant_app_category_ &&
            rhs.cso_tenant_app_subcategory() == cso_tenant_app_subcategory_ &&
            rhs.cso_tenant_app_groups() == cso_tenant_app_groups_ &&
            rhs.cso_tenant_app_risk() == cso_tenant_app_risk_ &&
            rhs.cso_tenant_app_service_tags() == cso_tenant_app_service_tags_;
        }

        const std::string name() { return name_; }
        const std::string cso_tenant_app_category() const { return cso_tenant_app_category_; }
        const std::string cso_tenant_app_subcategory() const { return cso_tenant_app_subcategory_; }
        const std::string cso_tenant_app_groups() const { return cso_tenant_app_groups_; }
        const std::string cso_tenant_app_risk() const { return cso_tenant_app_risk_; }
        const std::string cso_tenant_app_service_tags() const { return cso_tenant_app_service_tags_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string cso_tenant_app_category,
                     std::string cso_tenant_app_subcategory, std::string cso_tenant_app_groups,
                     std::string cso_tenant_app_risk, std::string cso_tenant_app_service_tags) {
             if (cso_tenant_app_category_ != cso_tenant_app_category)
                cso_tenant_app_category_ = cso_tenant_app_category;
             if (cso_tenant_app_subcategory_ != cso_tenant_app_subcategory)
                cso_tenant_app_subcategory_ = cso_tenant_app_subcategory;
             if (cso_tenant_app_groups_ != cso_tenant_app_groups)
                cso_tenant_app_groups_ = cso_tenant_app_groups;
             if (cso_tenant_app_risk_ != cso_tenant_app_risk)
                cso_tenant_app_risk_ = cso_tenant_app_risk;
             if (cso_tenant_app_service_tags_ != cso_tenant_app_service_tags)
                cso_tenant_app_service_tags_ = cso_tenant_app_service_tags;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            cso_tenant_app_category_;
        std::string            cso_tenant_app_subcategory_;
        std::string            cso_tenant_app_groups_;
        std::string            cso_tenant_app_risk_;
        std::string            cso_tenant_app_service_tags_;
};

typedef std::map<std::string, boost::shared_ptr<CsoHostnameRecord> > Chr_t;
typedef std::map<std::string, boost::shared_ptr<CsoApplicationRecord> > Car_t;
typedef std::map<std::string, boost::shared_ptr<CsoTenantApplicationRecord> > Ctar_t;

class CsoConfig {
    public:
        CsoConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection);
        ~CsoConfig();
        void AddCsoHostnameRecord(std::string name, std::string cso_hostaddr,
                                  std::string cso_tenant, std::string cso_location,
                                  std::string cso_device);
        void AddCsoApplicationRecord(std::string name, std::string cso_app_category,
                                  std::string cso_app_subcategory, std::string cso_default_app_groups,
                                  std::string cso_app_risk);
        void AddCsoTenantApplicationRecord(std::string name, std::string cso_tenant_app_category,
                                  std::string cso_tenant_app_subcategory, std::string cso_tenant_app_groups,
                                  std::string cso_tenant_app_risk, std::string cso_tenant_app_service_tags);
        void PollCsoHostnameRecords();
        void PollCsoApplicationRecords();
        void PollCsoTenantApplicationRecords();
        boost::shared_ptr<CsoHostnameRecord> GetCsoHostnameRecord(std::string name);
        boost::shared_ptr<CsoApplicationRecord> GetCsoApplicationRecord(std::string name);
        boost::shared_ptr<CsoTenantApplicationRecord> GetCsoTenantApplicationRecord(std::string name);

    private:
        void CsoHostnameRecordsHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        void CsoApplicationRecordsHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        void CsoTenantApplicationRecordsHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        Chr_t hostname_records_;
        Car_t application_records_;
        Ctar_t tenant_application_records_;
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection_;
};


#endif
