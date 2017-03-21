/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ANALYTICS_STRUCTURED_SYSLOG_CONFIG_H_
#define ANALYTICS_STRUCTURED_SYSLOG_CONFIG_H_


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include "http/client/vncapi.h"
#include "parser_util.h"
#include "configdb_connection.h"

class Options;

class HostnameRecord {
    public:
        explicit HostnameRecord(const std::string &name, const std::string &hostaddr,
        const std::string &tenant, const std::string &location, const std::string &device):
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

        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::string &hostaddr,
                     const std::string &tenant, const std::string &location,
                     const std::string &device) {
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
        bool GetandClearRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
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
        explicit ApplicationRecord(const std::string &name, const std::string &app_category,
        const std::string &app_subcategory, const std::string &app_groups,
        const std::string &app_risk, const std::string &app_service_tags):
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

        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::string &app_category,
                     const std::string &app_subcategory, const std::string &app_groups,
                     const std::string &app_risk, const std::string &app_service_tags) {
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
        bool GetandClearRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
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
        explicit TenantApplicationRecord(const std::string &name, const std::string &tenant_app_category,
        const std::string &tenant_app_subcategory, const std::string &tenant_app_groups,
        const std::string &tenant_app_risk, const std::string &tenant_app_service_tags):
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

        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::string &tenant_app_category,
                     const std::string &tenant_app_subcategory, const std::string &tenant_app_groups,
                     const std::string &tenant_app_risk, const std::string &tenant_app_service_tags) {
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
        bool GetandClearRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            tenant_app_category_;
        std::string            tenant_app_subcategory_;
        std::string            tenant_app_groups_;
        std::string            tenant_app_risk_;
        std::string            tenant_app_service_tags_;
};

class MessageConfig {
    public:
        explicit MessageConfig(const std::string &name, const std::vector< std::string > &tags,
        const std::vector< std::string > &ints, bool process_and_store, bool forward,
        bool process_before_forward):
            refreshed_(true), name_(name), tags_(tags), ints_(ints), process_and_store_(process_and_store),
            forward_(forward), process_before_forward_(process_before_forward) {
        }
        bool operator==(const MessageConfig &rhs) {
            return rhs.IsMe(name_) &&  std::equal(rhs.tags().begin(), rhs.tags().end(), tags_.begin()) &&
            std::equal(rhs.ints().begin(), rhs.ints().end(), ints_.begin()) && rhs.forward() == forward_ &&
            rhs.process_and_store() == process_and_store_ &&
            rhs.process_before_forward() == process_before_forward_;
        }

        const std::string name() { return name_; }
        const std::vector< std::string > tags() const { return tags_; }
        const std::vector< std::string > ints() const { return ints_; }
        const bool process_and_store() const { return process_and_store_; }
        const bool forward() const { return forward_; }
        const bool process_before_forward() const { return process_before_forward_; }
        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::vector< std::string > &tags,
                     const std::vector< std::string > &ints, bool process_and_store,
                     bool forward, bool process_before_forward) {
             if (!(std::equal(tags.begin(), tags.end(), tags_.begin()))) {
                tags_ = tags;
             }
             if (!(std::equal(ints.begin(), ints.end(), ints_.begin()))) {
                ints_ = ints;
             }
             if (process_and_store_ != process_and_store)
                process_and_store_ = process_and_store;
             if (forward_ != forward)
                forward_ = forward;
             if (process_before_forward_ != process_before_forward)
                process_before_forward_ = process_before_forward;

            refreshed_ = true;
        }
        bool GetandClearRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                        refreshed_;
        std::string                 name_;
        std::vector< std::string >  tags_;
        std::vector< std::string >  ints_;
        bool                        process_and_store_;
        bool                        forward_;
        bool                        process_before_forward_;
};

typedef std::map<std::string, boost::shared_ptr<HostnameRecord> > Chr_t;
typedef std::map<std::string, boost::shared_ptr<ApplicationRecord> > Car_t;
typedef std::map<std::string, boost::shared_ptr<TenantApplicationRecord> > Ctar_t;
typedef std::map<std::string, boost::shared_ptr<MessageConfig> > Cmc_t;

class StructuredSyslogConfig {
    public:
        StructuredSyslogConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection);
        ~StructuredSyslogConfig();
        void AddHostnameRecord(const std::string &name, const std::string &hostaddr,
                                  const std::string &tenant, const std::string &location,
                                  const std::string &device);
        void AddApplicationRecord(const std::string &name, const std::string &app_category,
                                  const std::string &app_subcategory, const std::string &app_groups,
                                  const std::string &app_risk, const std::string &app_service_tags);
        void AddTenantApplicationRecord(const std::string &name, const std::string &tenant_app_category,
                                  const std::string &tenant_app_subcategory, const std::string &tenant_app_groups,
                                  const std::string &tenant_app_risk, const std::string &tenant_app_service_tags);
        void AddMessageConfig(const std::string &name, const std::vector< std::string > &tags,
                                  const std::vector< std::string > &ints, bool process_and_store,
                                  const std::string &forward);
        void PollHostnameRecords();
        void PollApplicationRecords();
        void PollMessageConfigs();
        boost::shared_ptr<HostnameRecord> GetHostnameRecord(const std::string &name);
        boost::shared_ptr<ApplicationRecord> GetApplicationRecord(const std::string &name);
        boost::shared_ptr<TenantApplicationRecord> GetTenantApplicationRecord(const std::string &name);
        boost::shared_ptr<MessageConfig> GetMessageConfig(const std::string &name);

    private:
        void HostnameRecordsHandler(contrail_rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    const std::string &version, int status, const std::string &reason,
                    std::map<std::string, std::string> *headers);
        void ApplicationRecordsHandler(contrail_rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    const std::string &version, int status, const std::string &reason,
                    std::map<std::string, std::string> *headers);
        void MessageConfigsHandler(contrail_rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    const std::string &version, int status, const std::string &reason,
                    std::map<std::string, std::string> *headers);

        Chr_t hostname_records_;
        Car_t application_records_;
        Ctar_t tenant_application_records_;
        Cmc_t message_configs_;
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection_;
};


#endif
