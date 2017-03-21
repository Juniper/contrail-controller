/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include <boost/regex.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "analytics_types.h"
#include "structured_syslog_config.h"
#include "http/client/vncapi.h"
#include "options.h"
#include <base/logging.h>

StructuredSyslogConfig::StructuredSyslogConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection)
          : cfgdb_connection_(cfgdb_connection) {

}

StructuredSyslogConfig::~StructuredSyslogConfig() {
    hostname_records_.erase(hostname_records_.begin(),
                            hostname_records_.end());
    application_records_.erase(application_records_.begin(),
                               application_records_.end());
    tenant_application_records_.erase(tenant_application_records_.begin(),
                                      tenant_application_records_.end());
}

void
StructuredSyslogConfig::PollHostnameRecords() {
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("structured_syslog_hostaddr");
        fields.push_back("structured_syslog_tenant");
        fields.push_back("structured_syslog_location");
        fields.push_back("structured_syslog_device");

        cfgdb_connection_->GetVnc()->GetConfig("structured-syslog-hostname-record", ids,
                filters, parents, refs,
                fields, boost::bind(&StructuredSyslogConfig::HostnameRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
StructuredSyslogConfig::PollApplicationRecords() {
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("structured_syslog_app_category");
        fields.push_back("structured_syslog_app_subcategory");
        fields.push_back("structured_syslog_app_groups");
        fields.push_back("structured_syslog_app_risk");
        fields.push_back("structured_syslog_app_service_tags");

        cfgdb_connection_->GetVnc()->GetConfig("structured-syslog-application-record", ids,
                filters, parents, refs,
                fields, boost::bind(&StructuredSyslogConfig::ApplicationRecordsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
StructuredSyslogConfig::PollMessageConfigs() {
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("display_name");
        fields.push_back("structured_syslog_message_tagged_fields");
        fields.push_back("structured_syslog_message_integer_fields");
        fields.push_back("structured_syslog_message_process_and_store");
        fields.push_back("structured_syslog_message_forward");

        cfgdb_connection_->GetVnc()->GetConfig("structured-syslog-message", ids,
                filters, parents, refs,
                fields, boost::bind(&StructuredSyslogConfig::MessageConfigsHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
StructuredSyslogConfig::HostnameRecordsHandler(contrail_rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            const std::string &version, int status, const std::string &reason,
            std::map<std::string, std::string> *headers) {
    if (jdoc.IsObject() && jdoc.HasMember("structured-syslog-hostname-records")) {
        for (contrail_rapidjson::SizeType j = 0;
                    j < jdoc["structured-syslog-hostname-records"].Size(); j++) {
                const contrail_rapidjson::Value& hr = jdoc["structured-syslog-hostname-records"][j];
                std::string name, hostaddr, tenant, location, device;

                name = hr["display_name"].GetString();
                if (hr.HasMember("structured_syslog_hostaddr")) {
                    hostaddr = hr["structured_syslog_hostaddr"].GetString();
                }
                if (hr.HasMember("structured_syslog_tenant")) {
                    tenant = hr["structured_syslog_tenant"].GetString();
                }
                if (hr.HasMember("structured_syslog_location")) {
                    location = hr["structured_syslog_location"].GetString();
                }
                if (hr.HasMember("structured_syslog_device")) {
                    device = hr["structured_syslog_device"].GetString();
                }
                LOG(DEBUG, "Adding HostnameRecord: " << name);
                AddHostnameRecord(name, hostaddr, tenant,
                                     location, device);
        }
        Chr_t::iterator cit = hostname_records_.begin();
        while (cit != hostname_records_.end()) {
            Chr_t::iterator dit = cit++;
            if (!dit->second->GetandClearRefreshed()) {
                LOG(DEBUG, "Erasing HostnameRecord: " << dit->second->name());
                hostname_records_.erase(dit);
            }
        }
        return;
    } else {
        cfgdb_connection_->RetryNextApi();
    }

}

void
StructuredSyslogConfig::ApplicationRecordsHandler(contrail_rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            const std::string &version, int status, const std::string &reason,
            std::map<std::string, std::string> *headers) {
    if (jdoc.IsObject() && jdoc.HasMember("structured-syslog-application-records")) {
        for (contrail_rapidjson::SizeType j = 0;
                    j < jdoc["structured-syslog-application-records"].Size(); j++) {
                const contrail_rapidjson::Value& ar = jdoc["structured-syslog-application-records"][j];
                std::string name, app_category, app_subcategory,
                            app_groups, app_risk, app_service_tags;

                name = ar["display_name"].GetString();
                if (ar.HasMember("structured_syslog_app_category")) {
                    app_category = ar["structured_syslog_app_category"].GetString();
                }
                if (ar.HasMember("structured_syslog_app_subcategory")) {
                    app_subcategory = ar["structured_syslog_app_subcategory"].GetString();
                }
                if (ar.HasMember("structured_syslog_app_groups")) {
                    app_groups = ar["structured_syslog_app_groups"].GetString();
                }
                if (ar.HasMember("structured_syslog_app_risk")) {
                    app_risk = ar["structured_syslog_app_risk"].GetString();
                }
                if (ar.HasMember("structured_syslog_app_service_tags")) {
                    app_service_tags = ar["structured_syslog_app_service_tags"].GetString();
                }

                const contrail_rapidjson::Value& fq_name = ar["fq_name"];
                std::string tenant_name = fq_name[1].GetString();
                if (tenant_name.compare("default-global-analytics-config") == 0) {
                    LOG(DEBUG, "Adding ApplicationRecord: " << name);
                    AddApplicationRecord(name, app_category, app_subcategory,
                                            app_groups, app_risk, app_service_tags);
                }
                else {
                    std::string apprec_name;
                    apprec_name =  tenant_name + '/' + name;
                    LOG(DEBUG, "Adding TenantApplicationRecord: " << apprec_name);
                    AddTenantApplicationRecord(apprec_name, app_category, app_subcategory,
                                            app_groups, app_risk, app_service_tags);
                }
        }
        Car_t::iterator cit = application_records_.begin();
        while (cit != application_records_.end()) {
            Car_t::iterator dit = cit++;
            if (!dit->second->GetandClearRefreshed()) {
                LOG(DEBUG, "Erasing ApplicationRecord: " << dit->second->name());
                application_records_.erase(dit);
            }
        }
        Ctar_t::iterator ctit = tenant_application_records_.begin();
        while (ctit != tenant_application_records_.end()) {
            Ctar_t::iterator dtit = ctit++;
            if (!dtit->second->GetandClearRefreshed()) {
                LOG(DEBUG, "Erasing TenantApplicationRecord: " << dtit->second->name());
                tenant_application_records_.erase(dtit);
            }
        }

        return;
    } else {
        cfgdb_connection_->RetryNextApi();
    }

}

void
StructuredSyslogConfig::MessageConfigsHandler(contrail_rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            const std::string &version, int status, const std::string &reason,
            std::map<std::string, std::string> *headers) {
    if (jdoc.IsObject() && jdoc.HasMember("structured-syslog-messages")) {
        for (contrail_rapidjson::SizeType j = 0;
                    j < jdoc["structured-syslog-messages"].Size(); j++) {
                const contrail_rapidjson::Value& hr = jdoc["structured-syslog-messages"][j];
                std::vector< std::string > ints;
                std::vector< std::string > tags;
                std::string name, forward;
                bool process_and_store = false;

                name = hr["display_name"].GetString();
                if (hr.HasMember("structured_syslog_message_tagged_fields")) {
                    const contrail_rapidjson::Value& tagged_fields = hr["structured_syslog_message_tagged_fields"];
                    const contrail_rapidjson::Value& tag_array = tagged_fields["field_names"];
                    assert(tag_array.IsArray());
                    for (contrail_rapidjson::SizeType i = 0; i < tag_array.Size(); i++)
                        tags.push_back(tag_array[i].GetString());
                }
                if (hr.HasMember("structured_syslog_message_integer_fields")) {
                    const contrail_rapidjson::Value& integer_fields = hr["structured_syslog_message_integer_fields"];
                    const contrail_rapidjson::Value& int_array = integer_fields["field_names"];
                    assert(int_array.IsArray());
                    for (contrail_rapidjson::SizeType i = 0; i < int_array.Size(); i++)
                        ints.push_back(int_array[i].GetString());
                }
                if (hr.HasMember("structured_syslog_message_forward")) {
                    forward = hr["structured_syslog_message_forward"].GetString();
                }
                if (hr.HasMember("structured_syslog_message_process_and_store")) {
                    process_and_store = hr["structured_syslog_message_process_and_store"].GetBool();
                }
                LOG(DEBUG, "Adding MessageConfig: " << name);
                AddMessageConfig(name, tags, ints, process_and_store, forward);
        }
        Cmc_t::iterator cit = message_configs_.begin();
        while (cit != message_configs_.end()) {
            Cmc_t::iterator dit = cit++;
            if (!dit->second->GetandClearRefreshed()) {
                LOG(DEBUG, "Erasing HostnameRecord: " << dit->second->name());
                message_configs_.erase(dit);
            }
        }
        return;
    } else {
        cfgdb_connection_->RetryNextApi();
    }

}

boost::shared_ptr<HostnameRecord>
StructuredSyslogConfig::GetHostnameRecord(const std::string &name) {
    Chr_t::iterator it = hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        return it->second;
    }
    return boost::shared_ptr<HostnameRecord>();
}

void
StructuredSyslogConfig::AddHostnameRecord(const std::string &name,
        const std::string &hostaddr, const std::string &tenant,
        const std::string &location, const std::string &device) {
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
StructuredSyslogConfig::GetApplicationRecord(const std::string &name) {
    Car_t::iterator it = application_records_.find(name);
    if (it  != application_records_.end()) {
        return it->second;
    }
    return boost::shared_ptr<ApplicationRecord>();
}

void
StructuredSyslogConfig::AddApplicationRecord(const std::string &name,
        const std::string &app_category, const std::string &app_subcategory,
        const std::string &app_groups, const std::string &app_risk,
        const std::string &app_service_tags) {
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
StructuredSyslogConfig::GetTenantApplicationRecord(const std::string &name) {
    Ctar_t::iterator it = tenant_application_records_.find(name);
    if (it  != tenant_application_records_.end()) {
        return it->second;
    }
    return boost::shared_ptr<TenantApplicationRecord>();
}

void
StructuredSyslogConfig::AddTenantApplicationRecord(const std::string &name,
        const std::string &tenant_app_category, const std::string &tenant_app_subcategory,
        const std::string &tenant_app_groups, const std::string &tenant_app_risk,
        const std::string &tenant_app_service_tags) {
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

boost::shared_ptr<MessageConfig>
StructuredSyslogConfig::GetMessageConfig(const std::string &name) {
    Cmc_t::iterator it = message_configs_.find(name);
    if (it  != message_configs_.end()) {
        /* exact match */
        return it->second;
    }
    /* no exact match, look for match based on regex */
    Cmc_t::iterator cit = message_configs_.begin();
    Cmc_t::iterator end = message_configs_.end();
    Cmc_t::iterator match = end;
    while (cit != end) {
        boost::regex pattern;
        Cmc_t::iterator dit = cit++;
        boost::match_results<std::string::const_iterator> what;
        boost::match_flag_type flags = boost::match_default;
        std::string::const_iterator name_start = name.begin(), name_end = name.end();
        try {
            pattern = boost::regex(dit->second->name());
        }
        catch (boost::regex_error& e) {
            LOG(DEBUG, "skipping invalid regex pattern: " << dit->second->name());
            continue;
        }
        if(regex_search(name_start, name_end, what, pattern, flags)) {
            if ((match == end) || (match->second->name().length() < dit->second->name().length())) {
                match = dit;
            }
        }
    }
    if (match != end) {
        return match->second;
    }
    /* no match */
    return boost::shared_ptr<MessageConfig>();
}

void
StructuredSyslogConfig::AddMessageConfig(const std::string &name,
        const std::vector< std::string > &tags, const std::vector< std::string > &ints,
        bool process_and_store, const std::string &forward_action) {
    bool forward = false, process_before_forward = false;
    if (forward_action == "forward-unprocessed") {
        forward = true;
    }
    if (forward_action == "forward-processed") {
        forward = true;
        process_before_forward = true;
    }
    Cmc_t::iterator it = message_configs_.find(name);
    if (it  != message_configs_.end()) {
        it->second->Refresh(name, tags, ints, process_and_store, forward, process_before_forward);
    } else {
        boost::shared_ptr<MessageConfig> c(new MessageConfig(
                    name, tags, ints, process_and_store, forward, process_before_forward));
        message_configs_.insert(std::make_pair<std::string,
                boost::shared_ptr<MessageConfig> >(name, c));
    }
}
