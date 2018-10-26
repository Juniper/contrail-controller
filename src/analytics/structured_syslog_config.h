/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ANALYTICS_STRUCTURED_SYSLOG_CONFIG_H_
#define ANALYTICS_STRUCTURED_SYSLOG_CONFIG_H_


#include <map>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdint.h>
#include <cstdio>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/algorithm/string.hpp>
#include "config_client_collector.h"
#include "parser_util.h"

class Options;



struct IPNetwork
{
  IPNetwork(uint32_t lower, uint32_t upper, std::string& net_name) {
    address_begin = lower;
    address_end   = upper;
    id            = net_name;
  }

  bool operator < (const IPNetwork& other) const{
    return address_begin < other.address_begin;
  }

  uint32_t address_begin;
  uint32_t address_end;
  std::string id;

};

class HostnameRecord {
    public:
        explicit HostnameRecord(const std::string &name, const std::string &hostaddr,
        const std::string &tenant, const std::string &location,
        const std::string &device, const std::string &tags,
        const std::map< std::string, std::string > &linkmap):
            refreshed_(true), name_(name), hostaddr_(hostaddr), tenant_(tenant),
            location_(location), device_(device), tags_(tags), linkmap_(linkmap) {
        }
        bool operator==(const HostnameRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.hostaddr() == hostaddr_ &&
            rhs.tenant() == tenant_ && rhs.location() == location_ &&
            rhs.device() == device_ && rhs.tags() == tags_ &&
            rhs.linkmap().size() == linkmap_.size() &&
            std::equal(rhs.linkmap().begin(), rhs.linkmap().end(), linkmap_.begin());
        }

        const std::string name() { return name_; }
        const std::string hostaddr() const { return hostaddr_; }
        const std::string tenant() const { return tenant_; }
        const std::string location() const { return location_; }
        const std::string device() const { return device_; }
        const std::string tags() const { return tags_; }
        const std::map< std::string, std::string > linkmap() const { return linkmap_; }

        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::string &hostaddr,
                     const std::string &tenant, const std::string &location,
                     const std::string &device, const std::string &tags,
                     const std::map< std::string, std::string > &linkmap) {
             if (hostaddr_ != hostaddr)
                hostaddr_ = hostaddr;
             if (tenant_ != tenant)
                tenant_ = tenant;
             if (location_ != location)
                location_ = location;
             if (device_ != device)
                device_ = device;
             if (tags_ != tags)
                tags_ = tags;
            if (linkmap.size() != linkmap_.size()) {
                linkmap_ = linkmap;
            } else if (!(std::equal(linkmap.begin(), linkmap.end(), linkmap_.begin()))) {
                linkmap_ = linkmap;
            }
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
        std::string            tags_;
        std::map< std::string, std::string > linkmap_;
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
        bool process_before_forward, bool process_and_summarize, bool process_and_summarize_user):
            refreshed_(true), name_(name), tags_(tags), ints_(ints), process_and_store_(process_and_store),
            forward_(forward), process_before_forward_(process_before_forward), process_and_summarize_(process_and_summarize),
            process_and_summarize_user_(process_and_summarize_user) {
        }
        bool operator==(const MessageConfig &rhs) {
            return rhs.IsMe(name_) && rhs.tags().size() == tags_.size() &&
            std::equal(rhs.tags().begin(), rhs.tags().end(), tags_.begin()) &&
            rhs.ints().size() == ints_.size() &&
            std::equal(rhs.ints().begin(), rhs.ints().end(), ints_.begin()) &&
            rhs.forward() == forward_ &&
            rhs.process_and_store() == process_and_store_ &&
            rhs.process_before_forward() == process_before_forward_ &&
            rhs.process_and_summarize() == process_and_summarize_ &&
            rhs.process_and_summarize_user() == process_and_summarize_user_;
        }

        const std::string name() { return name_; }
        const std::vector< std::string > tags() const { return tags_; }
        const std::vector< std::string > ints() const { return ints_; }
        const bool process_and_store() const { return process_and_store_; }
        const bool process_and_summarize() const { return process_and_summarize_; }
        const bool process_and_summarize_user() const { return process_and_summarize_user_; }
        const bool forward() const { return forward_; }
        const bool process_before_forward() const { return process_before_forward_; }
        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::vector< std::string > &tags,
                     const std::vector< std::string > &ints, bool process_and_store,
                     bool forward, bool process_before_forward, bool process_and_summarize,
                     bool process_and_summarize_user) {
             if (tags.size() != tags_.size()) {
                 tags_ = tags;
             } else if (!(std::equal(tags.begin(), tags.end(), tags_.begin()))) {
                tags_ = tags;
             }
             if (ints.size() != ints_.size()) {
                ints_ = ints;
             } else if (!(std::equal(ints.begin(), ints.end(), ints_.begin()))) {
                ints_ = ints;
             }
             if (process_and_store_ != process_and_store)
                process_and_store_ = process_and_store;
             if (process_and_summarize_ != process_and_summarize)
                process_and_summarize_ = process_and_summarize;
             if (process_and_summarize_user_ != process_and_summarize_user)
                process_and_summarize_user_ = process_and_summarize_user;
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
        bool                        process_and_summarize_;
        bool                        process_and_summarize_user_;
};

class SlaProfileRecord {
    public:
        explicit SlaProfileRecord(const std::string &name, const std::string &sla_params):
            refreshed_(true), name_(name), sla_params_(sla_params) {
        }
        bool operator==(const SlaProfileRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.sla_params() == sla_params_;
        }

        const std::string name() { return name_; }
        const std::string sla_params() const { return sla_params_; }

        bool IsMe(const std::string &name) const { return name == name_; }
        void Refresh(const std::string &name, const std::string &sla_params) {
             if (sla_params_ != sla_params)
                sla_params_ = sla_params;

            refreshed_ = true;
        }
        bool GetandClearRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            sla_params_;
};

typedef std::map<std::string, boost::shared_ptr<HostnameRecord> > Chr_t;
typedef std::map<std::string, boost::shared_ptr<ApplicationRecord> > Car_t;
typedef std::map<std::string, boost::shared_ptr<TenantApplicationRecord> > Ctar_t;
typedef std::map<std::string, boost::shared_ptr<MessageConfig> > Cmc_t;
typedef std::map<std::string, boost::shared_ptr<SlaProfileRecord> > Csr_t;
typedef std::vector<IPNetwork> IPNetworks;
typedef std::map<std::string, IPNetworks> IPNetworks_map;

class StructuredSyslogConfig {
    public:
        StructuredSyslogConfig(ConfigClientCollector *config_client);
        ~StructuredSyslogConfig();
        uint32_t IPToUInt(std::string ip);
        std::vector<std::string> split_into_vector(std::string  str, char delimiter) ;
        bool AddNetwork(const std::string& key, const std::string& network, const std::string& mask, const std::string& net_name);
        bool RefreshNetworksMap(const std::string location);
        IPNetwork FindNetwork(std::string ip, std::string key);
        void AddHostnameRecord(const std::string &name, const std::string &hostaddr,
                                  const std::string &tenant, const std::string &location,
                                  const std::string &device, const std::string &tags,
                                  const std::map< std::string, std::string > &linkmap);
        void AddApplicationRecord(const std::string &name, const std::string &app_category,
                                  const std::string &app_subcategory, const std::string &app_groups,
                                  const std::string &app_risk, const std::string &app_service_tags);
        void AddTenantApplicationRecord(const std::string &name, const std::string &tenant_app_category,
                                  const std::string &tenant_app_subcategory, const std::string &tenant_app_groups,
                                  const std::string &tenant_app_risk, const std::string &tenant_app_service_tags);
        void AddMessageConfig(const std::string &name, const std::vector< std::string > &tags,
                                  const std::vector< std::string > &ints, bool process_and_store,
                                  const std::string &forward, bool process_and_summarize, bool process_and_summarize_user);
        void AddSlaProfileRecord(const std::string &name, const std::string &sla_params);
        boost::shared_ptr<HostnameRecord> GetHostnameRecord(const std::string &name);
        boost::shared_ptr<ApplicationRecord> GetApplicationRecord(const std::string &name);
        boost::shared_ptr<TenantApplicationRecord> GetTenantApplicationRecord(const std::string &name);
        boost::shared_ptr<MessageConfig> GetMessageConfig(const std::string &name);
        boost::shared_ptr<SlaProfileRecord> GetSlaProfileRecord(const std::string &name);
        void ReceiveConfig(const contrail_rapidjson::Document &jdoc, bool add_change);
    private:
        void HostnameRecordsHandler(const contrail_rapidjson::Document &jdoc,
                                    bool add_change);
        void ApplicationRecordsHandler(const contrail_rapidjson::Document &jdoc,
                                    bool add_change);
        void MessageConfigsHandler(const contrail_rapidjson::Document &jdoc,
                                    bool add_change);
        void SlaProfileRecordsHandler(const contrail_rapidjson::Document &jdoc,
                                    bool add_change);
        Chr_t hostname_records_;
        Car_t application_records_;
        Ctar_t tenant_application_records_;
        Cmc_t message_configs_;
        Csr_t sla_profile_records_;

        //networks_map_refresh_mutex should be used only to refresh networks map
        boost::mutex networks_map_refresh_mutex;
        IPNetworks_map networks_map_;
};


#endif
