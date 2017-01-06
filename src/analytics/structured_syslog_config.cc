/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "analytics_types.h"
#include "structured_syslog_config.h"
#include "http/client/vncapi.h"
#include "options.h"
#include <base/logging.h>

StructuredSyslogConfig::StructuredSyslogConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection)
          : cfgdb_connection_(cfgdb_connection)
{

}

StructuredSyslogConfig::~StructuredSyslogConfig()
{
    hostname_records_.erase(hostname_records_.begin(),
                            hostname_records_.end());
    application_records_.erase(application_records_.begin(),
                               application_records_.end());
    tenant_application_records_.erase(tenant_application_records_.begin(),
                                      tenant_application_records_.end());
}

void
StructuredSyslogConfig::PollHostnameRecords()
{
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("hostaddr");
        fields.push_back("tenant");
        fields.push_back("location");
        fields.push_back("device");

        cfgdb_connection_->GetVnc()->GetConfig("structured-syslog-hostname-record", ids,
                filters, parents, refs,
                fields, boost::bind(&StructuredSyslogConfig::HostnameRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
StructuredSyslogConfig::PollApplicationRecords()
{
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("app_category");
        fields.push_back("app_subcategory");
        fields.push_back("app_groups");
        fields.push_back("app_risk");
        fields.push_back("app_service_tags");

        cfgdb_connection_->GetVnc()->GetConfig("structured-syslog-application-record", ids,
                filters, parents, refs,
                fields, boost::bind(&StructuredSyslogConfig::ApplicationRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
StructuredSyslogConfig::HostnameRecordsHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("structured-syslog-hostname-records")) {
        for (rapidjson::SizeType j = 0;
                    j < jdoc["structured-syslog-hostname-records"].Size(); j++) {
                const rapidjson::Value& hr = jdoc["structured-syslog-hostname-records"][j];
                std::string name, hostaddr, tenant, location, device;

                name = hr["display_name"].GetString();
                if (hr.HasMember("hostaddr")) {
                    hostaddr = hr["hostaddr"].GetString();
                }
                if (hr.HasMember("tenant")) {
                    tenant = hr["tenant"].GetString();
                }
                if (hr.HasMember("location")) {
                    location = hr["location"].GetString();
                }
                if (hr.HasMember("device")) {
                    device = hr["device"].GetString();
                }
                LOG(DEBUG, "Adding HostnameRecord: " << name);
                AddHostnameRecord(name, hostaddr, tenant,
                                     location, device);
        }
        Chr_t::iterator cit = hostname_records_.begin();
        while (cit != hostname_records_.end()) {
            Chr_t::iterator dit = cit++;
            if (!dit->second->IsRefreshed()) {
                LOG(DEBUG, "Erasing HostnameRecord: " << dit->second->name());
                hostname_records_.erase(dit);
            }
        }
        return;
    } else {
                //Print Errors
    }
    cfgdb_connection_->RetryNextApi();
}

void
StructuredSyslogConfig::ApplicationRecordsHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("structured-syslog-application-records")) {
        for (rapidjson::SizeType j = 0;
                    j < jdoc["structured-syslog-application-records"].Size(); j++) {
                const rapidjson::Value& ar = jdoc["structured-syslog-application-records"][j];
                std::string name, app_category, app_subcategory,
                            app_groups, app_risk, app_service_tags;

                name = ar["display_name"].GetString();
                if (ar.HasMember("app_category")) {
                    app_category = ar["app_category"].GetString();
                }
                if (ar.HasMember("app_subcategory")) {
                    app_subcategory = ar["app_subcategory"].GetString();
                }
                if (ar.HasMember("app_groups")) {
                    app_groups = ar["app_groups"].GetString();
                }
                if (ar.HasMember("app_risk")) {
                    app_risk = ar["app_risk"].GetString();
                }
                if (ar.HasMember("app_service_tags")) {
                    app_service_tags = ar["app_service_tags"].GetString();
                }

                LOG(DEBUG, "Adding ApplicationRecord: " << name);
                AddApplicationRecord(name, app_category, app_subcategory,
                                        app_groups, app_risk, app_service_tags);
        }
        Car_t::iterator cit = application_records_.begin();
        while (cit != application_records_.end()) {
            Car_t::iterator dit = cit++;
            if (!dit->second->IsRefreshed()) {
                LOG(DEBUG, "Erasing ApplicationRecord: " << dit->second->name());
                application_records_.erase(dit);
            }
        }
        return;
    } else {
                //Print Errors
    }
    cfgdb_connection_->RetryNextApi();
}

boost::shared_ptr<HostnameRecord>
StructuredSyslogConfig::GetHostnameRecord(std::string name)
{
    Chr_t::iterator it = hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<HostnameRecord> null_ptr;
    return null_ptr;
}

void
StructuredSyslogConfig::AddHostnameRecord(std::string name, std::string hostaddr,
        std::string tenant, std::string location, std::string device)
{
    Chr_t::iterator it = hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        it->second->Refresh(name, hostaddr, tenant, location,
                           device);
    } else {
        boost::shared_ptr<HostnameRecord> c(new HostnameRecord(
                    name, hostaddr, tenant, location, device));
        hostname_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<HostnameRecord> >(name, c));
    }
}

boost::shared_ptr<ApplicationRecord>
StructuredSyslogConfig::GetApplicationRecord(std::string name)
{
    Car_t::iterator it = application_records_.find(name);
    if (it  != application_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<ApplicationRecord> null_ptr;
    return null_ptr;
}

void
StructuredSyslogConfig::AddApplicationRecord(std::string name, std::string app_category,
        std::string app_subcategory, std::string app_groups,
        std::string app_risk, std::string app_service_tags)
{
    Car_t::iterator it = application_records_.find(name);
    if (it  != application_records_.end()) {
        it->second->Refresh(name, app_category, app_subcategory, app_groups,
                           app_risk, app_service_tags);
    } else {
        boost::shared_ptr<ApplicationRecord> c(new ApplicationRecord(
                    name, app_category, app_subcategory, app_groups,
                    app_risk, app_service_tags));
        application_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<ApplicationRecord> >(name, c));
    }
}

boost::shared_ptr<TenantApplicationRecord>
StructuredSyslogConfig::GetTenantApplicationRecord(std::string name)
{
    Ctar_t::iterator it = tenant_application_records_.find(name);
    if (it  != tenant_application_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<TenantApplicationRecord> null_ptr;
    return null_ptr;
}

void
StructuredSyslogConfig::AddTenantApplicationRecord(std::string name, std::string tenant_app_category,
        std::string tenant_app_subcategory, std::string tenant_app_groups,
        std::string tenant_app_risk, std::string tenant_app_service_tags)
{
    Ctar_t::iterator it = tenant_application_records_.find(name);
    if (it  != tenant_application_records_.end()) {
        it->second->Refresh(name, tenant_app_category, tenant_app_subcategory,
                        tenant_app_groups, tenant_app_risk, tenant_app_service_tags);
    } else {
        boost::shared_ptr<TenantApplicationRecord> c(new TenantApplicationRecord(
                    name, tenant_app_category, tenant_app_subcategory,
                    tenant_app_groups, tenant_app_risk, tenant_app_service_tags));
        tenant_application_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<TenantApplicationRecord> >(name, c));
    }
}
