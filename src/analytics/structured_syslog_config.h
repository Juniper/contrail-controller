/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __STRUCTURED_SYSLOG_CONFIG_H__
#define __STRUCTURED_SYSLOG_CONFIG_H__


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "discovery/client/discovery_client.h"
#include "http/client/vncapi.h"
#include "parser_util.h"
#include "configdb_connection.h"

class Options;

class HostnameRecord {
    public:
        explicit HostnameRecord(std::string name, std::string hostaddr, std::string tenant,
        std::string location, std::string device):
            refreshed_(true), name_(name), hostaddr_(hostaddr), tenant_(tenant),
            location_(location), device_(device) {
        }
        bool operator==(const HostnameRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.hostaddr() == hostaddr_ &&
            rhs.tenant() == tenant_ && rhs.location() == location_ &&
            rhs.device() == device_;
        }

        const std::string name() { return name_; }
        const std::string hostaddr() const { return hostaddr_; }
        const std::string tenant() const { return tenant_; }
        const std::string location() const { return location_; }
        const std::string device() const { return device_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string hostaddr,
                     std::string tenant, std::string location,
                     std::string device) {
             if (hostaddr_ != hostaddr)
                hostaddr_ = hostaddr;
             if (tenant_ != tenant)
                tenant_ = tenant;
             if (location_ != location)
                location_ = location;
             if (device_ != device)
                device_ = device;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            hostaddr_;
        std::string            tenant_;
        std::string            location_;
        std::string            device_;
};

class ApplicationRecord {
    public:
        explicit ApplicationRecord(std::string name, std::string app_category,
        std::string app_subcategory, std::string app_groups, std::string app_risk,
        std::string app_service_tags):
            refreshed_(true), name_(name), app_category_(app_category),
            app_subcategory_(app_subcategory), app_groups_(app_groups),
            app_risk_(app_risk), app_service_tags_(app_service_tags) {
        }
        bool operator==(const ApplicationRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.app_category() == app_category_ &&
            rhs.app_subcategory() == app_subcategory_ &&
            rhs.app_groups() == app_groups_ &&
            rhs.app_risk() == app_risk_ &&
            rhs.app_service_tags() == app_service_tags_;
        }

        const std::string name() { return name_; }
        const std::string app_category() const { return app_category_; }
        const std::string app_subcategory() const { return app_subcategory_; }
        const std::string app_groups() const { return app_groups_; }
        const std::string app_risk() const { return app_risk_; }
        const std::string app_service_tags() const { return app_service_tags_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string app_category,
                     std::string app_subcategory, std::string app_groups,
                     std::string app_risk, std::string app_service_tags) {
             if (app_category_ != app_category)
                app_category_ = app_category;
             if (app_subcategory_ != app_subcategory)
                app_subcategory_ = app_subcategory;
             if (app_groups_ != app_groups)
                app_groups_ = app_groups;
             if (app_risk_ != app_risk)
                app_risk_ = app_risk;
             if (app_service_tags_ != app_service_tags)
                app_service_tags_ = app_service_tags;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            app_category_;
        std::string            app_subcategory_;
        std::string            app_groups_;
        std::string            app_risk_;
        std::string            app_service_tags_;
};

class TenantApplicationRecord {
    public:
        explicit TenantApplicationRecord(std::string name, std::string tenant_app_category,
        std::string tenant_app_subcategory, std::string tenant_app_groups,
        std::string tenant_app_risk, std::string tenant_app_service_tags):
            refreshed_(true), name_(name), tenant_app_category_(tenant_app_category),
            tenant_app_subcategory_(tenant_app_subcategory),
            tenant_app_groups_(tenant_app_groups), tenant_app_risk_(tenant_app_risk),
            tenant_app_service_tags_(tenant_app_service_tags) {
        }
        bool operator==(const TenantApplicationRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.tenant_app_category() == tenant_app_category_ &&
            rhs.tenant_app_subcategory() == tenant_app_subcategory_ &&
            rhs.tenant_app_groups() == tenant_app_groups_ &&
            rhs.tenant_app_risk() == tenant_app_risk_ &&
            rhs.tenant_app_service_tags() == tenant_app_service_tags_;
        }

        const std::string name() { return name_; }
        const std::string tenant_app_category() const { return tenant_app_category_; }
        const std::string tenant_app_subcategory() const { return tenant_app_subcategory_; }
        const std::string tenant_app_groups() const { return tenant_app_groups_; }
        const std::string tenant_app_risk() const { return tenant_app_risk_; }
        const std::string tenant_app_service_tags() const { return tenant_app_service_tags_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string tenant_app_category,
                     std::string tenant_app_subcategory, std::string tenant_app_groups,
                     std::string tenant_app_risk, std::string tenant_app_service_tags) {
             if (tenant_app_category_ != tenant_app_category)
                tenant_app_category_ = tenant_app_category;
             if (tenant_app_subcategory_ != tenant_app_subcategory)
                tenant_app_subcategory_ = tenant_app_subcategory;
             if (tenant_app_groups_ != tenant_app_groups)
                tenant_app_groups_ = tenant_app_groups;
             if (tenant_app_risk_ != tenant_app_risk)
                tenant_app_risk_ = tenant_app_risk;
             if (tenant_app_service_tags_ != tenant_app_service_tags)
                tenant_app_service_tags_ = tenant_app_service_tags;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            tenant_app_category_;
        std::string            tenant_app_subcategory_;
        std::string            tenant_app_groups_;
        std::string            tenant_app_risk_;
        std::string            tenant_app_service_tags_;
};

typedef std::map<std::string, boost::shared_ptr<HostnameRecord> > Chr_t;
typedef std::map<std::string, boost::shared_ptr<ApplicationRecord> > Car_t;
typedef std::map<std::string, boost::shared_ptr<TenantApplicationRecord> > Ctar_t;

class StructuredSyslogConfig {
    public:
        StructuredSyslogConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection);
        ~StructuredSyslogConfig();
        void AddHostnameRecord(std::string name, std::string hostaddr,
                                  std::string tenant, std::string location,
                                  std::string device);
        void AddApplicationRecord(std::string name, std::string app_category,
                                  std::string app_subcategory, std::string app_groups,
                                  std::string app_risk, std::string app_service_tags);
        void AddTenantApplicationRecord(std::string name, std::string tenant_app_category,
                                  std::string tenant_app_subcategory, std::string tenant_app_groups,
                                  std::string tenant_app_risk, std::string tenant_app_service_tags);
        void PollHostnameRecords();
        void PollApplicationRecords();
        boost::shared_ptr<HostnameRecord> GetHostnameRecord(std::string name);
        boost::shared_ptr<ApplicationRecord> GetApplicationRecord(std::string name);
        boost::shared_ptr<TenantApplicationRecord> GetTenantApplicationRecord(std::string name);

    private:
        void HostnameRecordsHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        void ApplicationRecordsHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        Chr_t hostname_records_;
        Car_t application_records_;
        Ctar_t tenant_application_records_;
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection_;
};


#endif
