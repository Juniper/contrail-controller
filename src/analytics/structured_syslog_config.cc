/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include <boost/regex.hpp>
#include "analytics_types.h"
#include "structured_syslog_config.h"
#include "options.h"
#include <base/logging.h>
#include <boost/bind.hpp>

StructuredSyslogConfig::StructuredSyslogConfig(ConfigClientCollector *config_client) {
    if (config_client) {
        config_client->RegisterConfigReceive("structured-systemlog", boost::bind(
                                 &StructuredSyslogConfig::ReceiveConfig, this, _1, _2));
    }
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
StructuredSyslogConfig::HostnameRecordsHandler(const contrail_rapidjson::Document &jdoc,
                                               bool add_update) {
    if (jdoc.IsObject() && jdoc.HasMember("structured_syslog_hostname_record")) {
        const contrail_rapidjson::Value& hr = jdoc["structured_syslog_hostname_record"];
        std::string name, hostaddr, tenant, location, device, tags;
        std::map< std::string, std::string > linkmap;

        if (hr.HasMember("fq_name")) {
            const contrail_rapidjson::Value& fq_name = hr["fq_name"];
            contrail_rapidjson::SizeType sz = fq_name.Size();
            name = fq_name[sz-1].GetString();
            LOG(DEBUG, "NAME got from fq_name: " << name);
        }
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
        if (hr.HasMember("structured_syslog_hostname_tags")) {
            tags = hr["structured_syslog_hostname_tags"].GetString();
        }
        if (hr.HasMember("structured_syslog_linkmap")) {
            const contrail_rapidjson::Value& linkmap_fields = hr["structured_syslog_linkmap"];
            const contrail_rapidjson::Value& links_array = linkmap_fields["links"];
            assert(links_array.IsArray());
            for (contrail_rapidjson::SizeType i = 0; i < links_array.Size(); i++) {
                linkmap.insert(std::make_pair<std::string,
                std::string >(links_array[i]["overlay"].GetString(),
                links_array[i]["underlay"].GetString()));
                LOG(DEBUG, "Adding HostnameRecord: " << name << " linkmap: "
                << links_array[i]["overlay"].GetString() << " : "
                << links_array[i]["underlay"].GetString());
            }
        }
        if (add_update) {
            LOG(DEBUG, "Adding HostnameRecord: " << name);
            AddHostnameRecord(name, hostaddr, tenant,
                                 location, device, tags, linkmap);
        } else {
            Chr_t::iterator cit = hostname_records_.find(name);
            if (cit != hostname_records_.end()) {
                LOG(DEBUG, "Erasing HostnameRecord: " << cit->second->name());
                hostname_records_.erase(cit);
            }
        }
        return;
    }
}

void
StructuredSyslogConfig::ApplicationRecordsHandler(const contrail_rapidjson::Document &jdoc,
                                                  bool add_update) {
    if (jdoc.IsObject() && jdoc.HasMember("structured_syslog_application_record")) {
        const contrail_rapidjson::Value& ar = jdoc["structured_syslog_application_record"];
        std::string name, app_category, app_subcategory,
                    app_groups, app_risk, app_service_tags;

        if (ar.HasMember("fq_name")) {
            const contrail_rapidjson::Value& fq_name = ar["fq_name"];
            contrail_rapidjson::SizeType sz = fq_name.Size();
            name = fq_name[sz-1].GetString();
            LOG(DEBUG, "NAME got from fq_name: " << name);
        }

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
            if (add_update) {
                LOG(DEBUG, "Adding ApplicationRecord: " << name);
                AddApplicationRecord(name, app_category, app_subcategory,
                                        app_groups, app_risk, app_service_tags);
            } else {
                Car_t::iterator cit = application_records_.find(name);
                if (cit != application_records_.end()) {
                    LOG(DEBUG, "Erasing ApplicationRecord: " << cit->second->name());
                    application_records_.erase(cit);
                }
           }
        }
        else {
            std::string apprec_name;
            apprec_name =  tenant_name + '/' + name;
            if (add_update) {
                LOG(DEBUG, "Adding TenantApplicationRecord: " << apprec_name);
                AddTenantApplicationRecord(apprec_name, app_category, app_subcategory,
                                        app_groups, app_risk, app_service_tags);
            } else {
                Ctar_t::iterator ctit = tenant_application_records_.find(apprec_name);
                if (ctit != tenant_application_records_.end()) {
                    LOG(DEBUG, "Erasing TenantApplicationRecord: " << ctit->second->name());
                    tenant_application_records_.erase(ctit);
                }
            }
        }

        return;
    }
}

void
StructuredSyslogConfig::MessageConfigsHandler(const contrail_rapidjson::Document &jdoc,
                                              bool add_update) {
    if (jdoc.IsObject() && jdoc.HasMember("structured_syslog_message")) {
        const contrail_rapidjson::Value& hr = jdoc["structured_syslog_message"];
        std::vector< std::string > ints;
        std::vector< std::string > tags;
        std::string name, forward;
        bool process_and_store = false;
        bool process_and_summarize = false;
        bool process_and_summarize_user = false;

        if (hr.HasMember("fq_name")) {
            const contrail_rapidjson::Value& fq_name = hr["fq_name"];
            contrail_rapidjson::SizeType sz = fq_name.Size();
            name = fq_name[sz-1].GetString();
            LOG(DEBUG, "NAME got from fq_name: " << name);
        }

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
        if (hr.HasMember("structured_syslog_message_process_and_summarize")) {
            process_and_summarize = hr["structured_syslog_message_process_and_summarize"].GetBool();
        }
        if (hr.HasMember("structured_syslog_message_process_and_summarize_user")) {
            process_and_summarize_user = hr["structured_syslog_message_process_and_summarize_user"].GetBool();
        }
        if (add_update) {
            LOG(DEBUG, "Adding MessageConfig: " << name);
            AddMessageConfig(name, tags, ints, process_and_store, forward, process_and_summarize, process_and_summarize_user);
        } else {
            Cmc_t::iterator cit = message_configs_.find(name);
            if (cit != message_configs_.end()) {
                LOG(DEBUG, "Erasing MessageConfig: " << cit->second->name());
                message_configs_.erase(cit);
            }
        }
        return;
    }

}

void
StructuredSyslogConfig::SlaProfileRecordsHandler(const contrail_rapidjson::Document &jdoc,
                                                 bool add_update) {
    if (jdoc.IsObject() && jdoc.HasMember("structured_syslog_sla_profile")) {
        const contrail_rapidjson::Value& slar = jdoc["structured_syslog_sla_profile"];
        std::string name, sla_params, tenant_name, slarec_name;

        if (slar.HasMember("fq_name")) {
            const contrail_rapidjson::Value& fq_name = slar["fq_name"];
            contrail_rapidjson::SizeType sz = fq_name.Size();
            name = fq_name[sz-1].GetString();
            tenant_name = fq_name[1].GetString();
            slarec_name =  tenant_name + '/' + name;
            LOG(DEBUG, "NAME got from fq_name: " << name);
        }
        if (slar.HasMember("structured_syslog_sla_params")) {
            sla_params = slar["structured_syslog_sla_params"].GetString();
        }
        if (add_update) {
            LOG(DEBUG, "Adding SlaProfileRecord: " << slarec_name);
            AddSlaProfileRecord(slarec_name, sla_params);
        } else {
            Csr_t::iterator cit = sla_profile_records_.find(slarec_name);
            if (cit != sla_profile_records_.end()) {
                LOG(DEBUG, "Erasing SlaProfileRecord: " << cit->second->name());
                sla_profile_records_.erase(cit);
            }
        }
        return;
    }
}

void
StructuredSyslogConfig::ReceiveConfig(const contrail_rapidjson::Document &jdoc, bool add_change) {
    HostnameRecordsHandler(jdoc, add_change);
    ApplicationRecordsHandler(jdoc, add_change);
    MessageConfigsHandler(jdoc, add_change);
    SlaProfileRecordsHandler(jdoc, add_change);
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
        const std::string &location, const std::string &device,
        const std::string &tags, const std::map< std::string, std::string > &linkmap) {
    Chr_t::iterator it = hostname_records_.find(name);
    if (it  != hostname_records_.end()) {
        it->second->Refresh(name, hostaddr, tenant, location,
                           device, tags, linkmap);
    } else {
        boost::shared_ptr<HostnameRecord> c(new HostnameRecord(
                    name, hostaddr, tenant, location, device, tags, linkmap));
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

boost::shared_ptr<SlaProfileRecord>
StructuredSyslogConfig::GetSlaProfileRecord(const std::string &name) {
    Csr_t::iterator it = sla_profile_records_.find(name);
    if (it  != sla_profile_records_.end()) {
        return it->second;
    }
    return boost::shared_ptr<SlaProfileRecord>();
}

void
StructuredSyslogConfig::AddSlaProfileRecord(const std::string &name,
        const std::string &sla_params) {
    Csr_t::iterator it = sla_profile_records_.find(name);
    if (it  != sla_profile_records_.end()) {
        it->second->Refresh(name, sla_params);
    } else {
        boost::shared_ptr<SlaProfileRecord> c(new SlaProfileRecord(
                    name, sla_params));
        sla_profile_records_.insert(std::make_pair<std::string,
                boost::shared_ptr<SlaProfileRecord> >(name, c));
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
        bool process_and_store, const std::string &forward_action, bool process_and_summarize, 
        bool process_and_summarize_user) {
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
        it->second->Refresh(name, tags, ints, process_and_store, forward, process_before_forward, 
                            process_and_summarize, process_and_summarize_user);
    } else {
        boost::shared_ptr<MessageConfig> c(new MessageConfig(
                    name, tags, ints, process_and_store, forward, process_before_forward, 
		    process_and_summarize, process_and_summarize_user));
        message_configs_.insert(std::make_pair<std::string,
                boost::shared_ptr<MessageConfig> >(name, c));
    }
}
