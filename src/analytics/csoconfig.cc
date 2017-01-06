/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "analytics_types.h"
#include "csoconfig.h"
#include "http/client/vncapi.h"
#include "options.h"
#include <base/logging.h>

CsoConfig::CsoConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection)
          : cfgdb_connection_(cfgdb_connection)
{

}

CsoConfig::~CsoConfig()
{
    hostname_records_.erase(hostname_records_.begin(),
                            hostname_records_.end());
    application_records_.erase(application_records_.begin(),
                               application_records_.end());
    tenant_application_records_.erase(tenant_application_records_.begin(),
                                      tenant_application_records_.end());
}

void
CsoConfig::PollCsoHostnameRecords()
{
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("cso_hostaddr");
        fields.push_back("cso_tenant");
        fields.push_back("cso_location");
        fields.push_back("cso_device");

        cfgdb_connection_->GetVnc()->GetConfig("cso-hostname-record", ids,
                filters, parents, refs,
                fields, boost::bind(&CsoConfig::CsoHostnameRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
CsoConfig::PollCsoApplicationRecords()
{
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("cso_app_category");
        fields.push_back("cso_app_subcategory");
        fields.push_back("cso_default_app_groups");
        fields.push_back("cso_app_risk");

        cfgdb_connection_->GetVnc()->GetConfig("cso-application-record", ids,
                filters, parents, refs,
                fields, boost::bind(&CsoConfig::CsoApplicationRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
CsoConfig::PollCsoTenantApplicationRecords()
{
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("cso_tenant_app_category");
        fields.push_back("cso_tenant_app_subcategory");
        fields.push_back("cso_tenant_app_groups");
        fields.push_back("cso_tenant_app_risk");
        fields.push_back("cso_tenant_app_service_tags");

        cfgdb_connection_->GetVnc()->GetConfig("cso-tenant-application-record", ids,
                filters, parents, refs,
                fields, boost::bind(&CsoConfig::CsoTenantApplicationRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
CsoConfig::CsoHostnameRecordsHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("cso-hostname-records")) {
        for (rapidjson::SizeType j = 0;
                    j < jdoc["cso-hostname-records"].Size(); j++) {
                const rapidjson::Value& hr = jdoc["cso-hostname-records"][j];
                std::string name, cso_hostaddr, cso_tenant, cso_location, cso_device;

                name = hr["display_name"].GetString();
                if (hr.HasMember("cso_hostaddr")) {
                    cso_hostaddr = hr["cso_hostaddr"].GetString();
                }
                if (hr.HasMember("cso_tenant")) {
                    cso_tenant = hr["cso_tenant"].GetString();
                }
                if (hr.HasMember("cso_location")) {
                    cso_location = hr["cso_location"].GetString();
                }
                if (hr.HasMember("cso_device")) {
                    cso_device = hr["cso_device"].GetString();
                }
                LOG(DEBUG, "Adding HostnameRecord: " << name);
                AddCsoHostnameRecord(name, cso_hostaddr, cso_tenant,
                                     cso_location, cso_device);
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
CsoConfig::CsoApplicationRecordsHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("cso-application-records")) {
        for (rapidjson::SizeType j = 0;
                    j < jdoc["cso-application-records"].Size(); j++) {
                const rapidjson::Value& hr = jdoc["cso-application-records"][j];
                std::string name, cso_app_category, cso_app_subcategory,
                            cso_default_app_groups, cso_app_risk;

                name = hr["display_name"].GetString();
                if (hr.HasMember("cso_app_category")) {
                    cso_app_category = hr["cso_app_category"].GetString();
                }
                if (hr.HasMember("cso_app_subcategory")) {
                    cso_app_subcategory = hr["cso_app_subcategory"].GetString();
                }
                if (hr.HasMember("cso_default_app_groups")) {
                    cso_default_app_groups = hr["cso_default_app_groups"].GetString();
                }
                if (hr.HasMember("cso_app_risk")) {
                    cso_app_risk = hr["cso_app_risk"].GetString();
                }
                LOG(DEBUG, "Adding ApplicationRecord: " << name);
                AddCsoApplicationRecord(name, cso_app_category, cso_app_subcategory,
                                        cso_default_app_groups, cso_app_risk);
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

void
CsoConfig::CsoTenantApplicationRecordsHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("cso-tenant-application-records")) {
        for (rapidjson::SizeType j = 0;
                    j < jdoc["cso-tenant-application-records"].Size(); j++) {
                const rapidjson::Value& hr = jdoc["cso-tenant-application-records"][j];
                std::string name, cso_tenant_app_category, cso_tenant_app_subcategory,
                    cso_tenant_app_groups, cso_tenant_app_risk, cso_tenant_app_service_tags;

                name = hr["display_name"].GetString();
                if (hr.HasMember("cso_tenant_app_category")) {
                    cso_tenant_app_category = hr["cso_tenant_app_category"].GetString();
                }
                if (hr.HasMember("cso_tenant_app_subcategory")) {
                    cso_tenant_app_subcategory = hr["cso_tenant_app_subcategory"].GetString();
                }
                if (hr.HasMember("cso_tenant_app_groups")) {
                    cso_tenant_app_groups = hr["cso_tenant_app_groups"].GetString();
                }
                if (hr.HasMember("cso_tenant_app_risk")) {
                    cso_tenant_app_risk = hr["cso_tenant_app_risk"].GetString();
                }
                if (hr.HasMember("cso_tenant_app_service_tags")) {
                    cso_tenant_app_service_tags = hr["cso_tenant_app_service_tags"].GetString();
                }

                LOG(DEBUG, "Adding TenantApplicationRecord: " << name);
                AddCsoTenantApplicationRecord(name, cso_tenant_app_category,
                        cso_tenant_app_subcategory, cso_tenant_app_groups,
                        cso_tenant_app_risk, cso_tenant_app_service_tags);
        }
        Ctar_t::iterator cit = tenant_application_records_.begin();
        while (cit != tenant_application_records_.end()) {
            Ctar_t::iterator dit = cit++;
            if (!dit->second->IsRefreshed()) {
                LOG(DEBUG, "Erasing TenantApplicationRecord: " << dit->second->name());
                tenant_application_records_.erase(dit);
            }
        }
        return;
    } else {
                //Print Errors
    }
    cfgdb_connection_->RetryNextApi();
}

boost::shared_ptr<CsoHostnameRecord>
CsoConfig::GetCsoHostnameRecord(std::string name)
{
    Chr_t::iterator it = hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<CsoHostnameRecord> null_ptr;
    return null_ptr;
}

void
CsoConfig::AddCsoHostnameRecord(std::string name, std::string cso_hostaddr,
        std::string cso_tenant, std::string cso_location, std::string cso_device)
{
    Chr_t::iterator it = hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        it->second->Refresh(name, cso_hostaddr, cso_tenant, cso_location,
                           cso_device);
    } else {
        boost::shared_ptr<CsoHostnameRecord> c(new CsoHostnameRecord(
                    name, cso_hostaddr, cso_tenant, cso_location, cso_device));
        hostname_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<CsoHostnameRecord> >(name, c));
    }
}

boost::shared_ptr<CsoApplicationRecord>
CsoConfig::GetCsoApplicationRecord(std::string name)
{
    Car_t::iterator it = application_records_.find(name);
    if (it  != application_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<CsoApplicationRecord> null_ptr;
    return null_ptr;
}

void
CsoConfig::AddCsoApplicationRecord(std::string name, std::string cso_app_category,
        std::string cso_app_subcategory, std::string cso_default_app_groups,
        std::string cso_app_risk)
{
    Car_t::iterator it = application_records_.find(name);
    if (it  != application_records_.end()) {
        it->second->Refresh(name, cso_app_category, cso_app_subcategory, cso_default_app_groups,
                           cso_app_risk);
    } else {
        boost::shared_ptr<CsoApplicationRecord> c(new CsoApplicationRecord(
                    name, cso_app_category, cso_app_subcategory, cso_default_app_groups,
                    cso_app_risk));
        application_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<CsoApplicationRecord> >(name, c));
    }
}

boost::shared_ptr<CsoTenantApplicationRecord>
CsoConfig::GetCsoTenantApplicationRecord(std::string name)
{
    Ctar_t::iterator it = tenant_application_records_.find(name);
    if (it  != tenant_application_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<CsoTenantApplicationRecord> null_ptr;
    return null_ptr;
}

void
CsoConfig::AddCsoTenantApplicationRecord(std::string name, std::string cso_tenant_app_category,
        std::string cso_tenant_app_subcategory, std::string cso_tenant_app_groups,
        std::string cso_tenant_app_risk, std::string cso_tenant_app_service_tags)
{
    Ctar_t::iterator it = tenant_application_records_.find(name);
    if (it  != tenant_application_records_.end()) {
        it->second->Refresh(name, cso_tenant_app_category, cso_tenant_app_subcategory,
                        cso_tenant_app_groups, cso_tenant_app_risk, cso_tenant_app_service_tags);
    } else {
        boost::shared_ptr<CsoTenantApplicationRecord> c(new CsoTenantApplicationRecord(
                    name, cso_tenant_app_category, cso_tenant_app_subcategory,
                    cso_tenant_app_groups, cso_tenant_app_risk, cso_tenant_app_service_tags));
        tenant_application_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<CsoTenantApplicationRecord> >(name, c));
    }
}
