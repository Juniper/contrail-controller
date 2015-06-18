/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "loadbalancer_config.h"

#include <cerrno>
#include <fstream>
#include <boost/assign/list_of.hpp>
#include "base/logging.h"
#include "agent.h"
#include "init/agent_param.h"

#include "loadbalancer_properties.h"

using namespace std;
using boost::assign::map_list_of;

LoadbalancerConfig::LoadbalancerConfig(Agent *agent)
        : agent_(agent) {
}

void LoadbalancerConfig::GeneratePool(
    ostream *out, const boost::uuids::uuid &pool_id,
    const LoadbalancerProperties &props) const {
    const autogen::LoadbalancerPoolType &pool = props.pool_properties();

    ostringstream ostr;
    ostr << "  \"pool\":{" << endl
         << "     \"id\":\"" << pool_id << "\"," << endl
         << "     \"protocol\":\"" << pool.protocol << "\"," << endl
         << "     \"method\":\"" << pool.loadbalancer_method << "\"," << endl
         << "     \"admin-state\":" << std::boolalpha << pool.admin_state
         << endl
         << "  }," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateVip(
    ostream *out, const LoadbalancerProperties &props) const {

    const autogen::VirtualIpType &vip = props.vip_properties();
    ostringstream ostr;
    ostr << "  \"vip\":{" << endl
         << "     \"id\":\"" << props.vip_uuid() << "\"," << endl
         << "     \"address\":\"" << vip.address <<"\"," << endl
         << "     \"port\":" << vip.protocol_port << "," << endl
         << "     \"protocol\":\"" << vip.protocol <<"\"," << endl
         << "     \"connection-limit\":" << vip.connection_limit << ","
         << endl
         << "     \"persistence-cookie-name\": \""
         << vip.persistence_cookie_name << "\"," << endl
         << "     \"persistence-type\": \"" << vip.persistence_type << "\","
         << endl
         << "     \"admin-state\":" << std::boolalpha << vip.admin_state << endl
         << "  }," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateMembers(
    ostream *out, const LoadbalancerProperties &props) const {

    ostringstream ostr;
    ostr << "  \"members\":[" << endl;
    int count = 0;
    for (LoadbalancerProperties::MemberMap::const_iterator iter =
                 props.members().begin();
         iter != props.members().end(); ++iter) {
        const autogen::LoadbalancerMemberType &member = iter->second;
        if (count) {
            ostr << "," << endl;
        }
        ostr << "     {" << endl
             << "       \"id\":\"" << iter->first << "\"," << endl
             << "       \"address\":\"" << member.address << "\","  << endl
             << "       \"port\":" << member.protocol_port << "," << endl
             << "       \"weight\":" << member.weight << "," << endl
             << "       \"admin-state\":" << std::boolalpha
             << member.admin_state << endl
             << "     }";
        count++;
    }
    if (count) {
        ostr << endl;
    }
    ostr << "  ]," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateHealthMonitors(
    ostream *out, const LoadbalancerProperties &props) const {

    ostringstream ostr;
    ostr << "  \"healthmonitors\":[" << endl;
    int count = 0;
    for (LoadbalancerProperties::HealthmonitorMap::const_iterator iter =
                 props.healthmonitors().begin();
         iter != props.healthmonitors().end(); ++iter) {
        const autogen::LoadbalancerHealthmonitorType &hm = iter->second;
        if (count) {
            ostr << "," << endl;
        }
        ostr << "     {" << endl
             << "       \"id\":\"" << iter->first << "\"," << endl
             << "       \"type\": \"" << hm.monitor_type << "\","  << endl
             << "       \"delay\":" << hm.delay << "," << endl
             << "       \"timeout\":" << hm.timeout << "," << endl
             << "       \"max-retries\":" << hm.max_retries << "," << endl
             << "       \"http-method\": \"" << hm.http_method << "\","  << endl
             << "       \"url\": \"" << hm.url_path << "\","  << endl
             << "       \"expected-codes\": \"" << hm.expected_codes << "\","
             << endl
             << "       \"admin-state\":" << std::boolalpha << hm.admin_state
             << endl
             << "     }";
        count++;
    }
    if (count) {
        ostr << endl;
    }
    ostr << "  ]" << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateConfig(
    const string &filename, const boost::uuids::uuid &pool_id,
    const LoadbalancerProperties &props) const {
    ofstream fs(filename.c_str());
    if (fs.fail()) {
        LOG(ERROR, "File create " << filename << ": " << strerror(errno));
    }

    fs << "{" << endl;
    fs << "  \"ssl-crt\":\"" << agent_->params()->si_lb_ssl_cert_path()
       << "\"," << endl;

    if (!agent_->params()->si_lb_config_path().empty()) {
        fs << "  \"lb-cfg\":\"" << agent_->params()->si_lb_config_path()
            << "\"," << endl;
    }
    GeneratePool(&fs, pool_id, props);
    GenerateVip(&fs, props);
    GenerateMembers(&fs, props);
    GenerateHealthMonitors(&fs, props);
    fs << "}" << endl;
    fs.close();
}
