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
    hostname_records_.erase(hostname_records_.begin(), hostname_records_.end());
}

void
CsoConfig::PollCsoConfig()
{
    ReadConfig();
}

void
CsoConfig::ReadConfig()
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
        fields.push_back("cso_site");
        fields.push_back("cso_device");

        cfgdb_connection_->GetVnc()->GetConfig("cso-hostname-record", ids, filters, parents, refs,
                fields, boost::bind(&CsoConfig::CsoConfigHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
CsoConfig::CsoConfigHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("cso-hostname-records")) {
        for (rapidjson::SizeType j=0;
                    j < jdoc["cso-hostname-records"].Size(); j++) {
                const rapidjson::Value& hr = jdoc["cso-hostname-records"][j];
                std::string name, cso_hostaddr, cso_tenant, cso_site, cso_device;
                name = hr["display_name"].GetString();
                if (hr.HasMember("cso_hostaddr")) {
                    cso_hostaddr = hr["cso_hostaddr"].GetString();
                }
                if (hr.HasMember("cso_tenant")) {
                    cso_tenant = hr["cso_tenant"].GetString();
                }
                if (hr.HasMember("cso_site")) {
                    cso_site = hr["cso_site"].GetString();
                }
                if (hr.HasMember("cso_device")) {
                    cso_device = hr["cso_device"].GetString();
                }
                LOG(DEBUG, "Adding HostnameRecord: " << name);
                AddCsoHostnameRecord(name, cso_hostaddr, cso_tenant,
                                     cso_site, cso_device);
        }
        Chr_t::iterator cit=hostname_records_.begin();
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

boost::shared_ptr<CsoHostnameRecord>
CsoConfig::GetCsoHostnameRecord(std::string name)
{
    Chr_t::iterator it=hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        return it->second;
    }
    boost::shared_ptr<CsoHostnameRecord> null_ptr;
    return null_ptr;
}

void
CsoConfig::AddCsoHostnameRecord(std::string name, std::string cso_hostaddr,
        std::string cso_tenant, std::string cso_site, std::string cso_device)
{
    Chr_t::iterator it=hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        it->second->Refresh(name, cso_hostaddr, cso_tenant, cso_site,
                           cso_device);
    } else {
        boost::shared_ptr<CsoHostnameRecord> c(new CsoHostnameRecord(
                    name, cso_hostaddr, cso_tenant, cso_site, cso_device));
        hostname_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<CsoHostnameRecord> >(name, c));
    }
}


