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

#include "loadbalancer_pool_info.h"
#include "loadbalancer_pool.h"

using namespace std;
using boost::assign::map_list_of;

LoadbalancerConfig::LoadbalancerConfig(Agent *agent)
        : agent_(agent) {
}

void LoadbalancerConfig::GeneratePool(
    ostream *out, const boost::uuids::uuid &pool_id,
    const LoadBalancerPoolInfo &props, const std::string &indent) const {
    const autogen::LoadbalancerPoolType &pool = props.pool_properties();

    ostringstream ostr;
    ostr << indent << "   \"pool\":{" << endl
         << indent << "      \"id\":\"" << pool_id << "\"," << endl
         << indent << "      \"protocol\":\"" << pool.protocol << "\"," << endl
         << indent << "      \"method\":\"" << pool.loadbalancer_method << "\","
         << endl
         << indent << "      \"admin-state\":" << std::boolalpha
         << pool.admin_state << endl
         << indent << "   }," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateVip(
    ostream *out, const LoadBalancerPoolInfo &props) const {

    const autogen::VirtualIpType &vip = props.vip_properties();
    ostringstream ostr;
    ostr << "   \"vip\":{" << endl
         << "      \"id\":\"" << props.vip_uuid() << "\"," << endl
         << "      \"address\":\"" << vip.address <<"\"," << endl
         << "      \"port\":" << vip.protocol_port << "," << endl
         << "      \"protocol\":\"" << vip.protocol <<"\"," << endl
         << "      \"connection-limit\":" << vip.connection_limit << ","
         << endl
         << "      \"persistence-cookie-name\": \""
         << vip.persistence_cookie_name << "\"," << endl
         << "      \"persistence-type\": \"" << vip.persistence_type << "\","
         << endl
         << "      \"admin-state\":" << std::boolalpha << vip.admin_state << endl
         << "   }," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateMembers(
    ostream *out, const LoadBalancerPoolInfo &props, const std::string &indent) const {

    ostringstream ostr;
    ostr << indent << "   \"members\":[" << endl;
    int count = 0;
    for (LoadBalancerPoolInfo::MemberMap::const_iterator iter =
                 props.members().begin();
         iter != props.members().end(); ++iter) {
        const autogen::LoadbalancerMemberType &member = iter->second;
        if (count) {
            ostr << "," << endl;
        }
        ostr << indent << "     {" << endl
             << indent << "        \"id\":\"" << iter->first << "\"," << endl
             << indent << "        \"address\":\"" << member.address << "\","
             << endl
             << indent << "        \"port\":" << member.protocol_port << ","
             << endl
             << indent << "        \"weight\":" << member.weight << "," << endl
             << indent << "        \"admin-state\":" << std::boolalpha
             << member.admin_state << endl
             << indent << "     }";
        count++;
    }
    if (count) {
        ostr << endl;
    }
    ostr << indent << "   ]," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateHealthMonitors(
    ostream *out, const LoadBalancerPoolInfo &props, const std::string &indent) const {

    ostringstream ostr;
    ostr << indent << "   \"healthmonitors\":[" << endl;
    int count = 0;
    for (LoadBalancerPoolInfo::HealthmonitorMap::const_iterator iter =
                 props.healthmonitors().begin();
         iter != props.healthmonitors().end(); ++iter) {
        const autogen::LoadbalancerHealthmonitorType &hm = iter->second;
        if (count) {
            ostr << "," << endl;
        }
        ostr << indent << "     {" << endl
             << indent << "        \"id\":\"" << iter->first << "\"," << endl
             << indent << "        \"type\": \"" << hm.monitor_type << "\","
             << endl
             << indent << "        \"delay\":" << hm.delay << "," << endl
             << indent << "        \"timeout\":" << hm.timeout << "," << endl
             << indent << "        \"max-retries\":" << hm.max_retries << ","
             << endl
             << indent << "        \"http-method\": \"" << hm.http_method
             << "\"," << endl
             << indent << "        \"url\": \"" << hm.url_path << "\","  << endl
             << indent << "        \"expected-codes\": \"" << hm.expected_codes
             << "\","
             << endl
             << indent << "        \"admin-state\":" << std::boolalpha
             << hm.admin_state << endl
             << indent << "     }";
        count++;
    }
    if (count) {
        ostr << endl;
    }
    ostr << indent << "   ]," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateCustomAttributes(
    ostream *out, const LoadBalancerPoolInfo &props, const std::string &indent) const {
    const std::vector<autogen::KeyValuePair>
            &custom_attributes = props.custom_attributes();
    ostringstream ostr;
    autogen::KeyValuePairs::const_iterator curr_iter, next_iter, end_iter;
    curr_iter = custom_attributes.begin();
    end_iter = custom_attributes.end();

    ostr << indent << "   \"custom-attributes\":{" << endl;

    while (curr_iter != end_iter) {
        next_iter = curr_iter + 1;
        const autogen::KeyValuePair element = (*curr_iter);
        ostr << indent << "     \"" << element.key << "\":\"" << element.value
            << "\"";
        if (next_iter == end_iter) {
            ostr << endl;
            break;
        }
        ostr << "," << endl;
        curr_iter = next_iter;
    }
    ostr << indent << "   }" << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateConfig(
    const string &filename, const boost::uuids::uuid &pool_id,
    const LoadBalancerPoolInfo &props) const {
    ofstream fs(filename.c_str());
    if (fs.fail()) {
        LOG(ERROR, "File create " << filename << ": " << strerror(errno));
    }

    fs << "{" << endl;
    fs << "   \"ssl-crt\":\"" << agent_->params()->si_lb_ssl_cert_path()
       << "\"," << endl;
    GeneratePool(&fs, pool_id, props, "");
    GenerateVip(&fs, props);
    GenerateMembers(&fs, props, "");
    GenerateHealthMonitors(&fs, props, "");
    GenerateCustomAttributes(&fs, props, "");
    fs << "}" << endl;
    fs.close();
}

void LoadbalancerConfig::GenerateLoadbalancer(ostream *out,
                                              Loadbalancer *lb) const {
    ostringstream ostr;
    ostr << "   \"loadbalancer\":{" << endl
         << "      \"id\":\"" << lb->uuid() << "\"," << endl
         << "      \"status\":\"" << lb->lb_info().status << "\"," << endl
         << "      \"provision-status\":\"" << lb->lb_info().provisioning_status
         << "\"," << endl
         << "      \"operational-status\":\"" << lb->lb_info().operating_status
         << "\"," << endl
         << "      \"vip-subnet-id\":\"" << lb->lb_info().vip_subnet_id << "\","
         << endl
         << "      \"vip-address\":\"" << lb->lb_info().vip_address << "\","
         << endl
         << "      \"admin-state\":" << std::boolalpha
         << lb->lb_info().admin_state
         << endl
         << "   }," << endl;
    *out << ostr.str();
}

void LoadbalancerConfig::GenerateListeners(ostream *out,
                                           Loadbalancer *lb) const {
    ostringstream ostr;
    *out << "   \"listeners\":[" << endl;
    int count = 0;
    for (Loadbalancer::ListenerMap::const_iterator iter =
         lb->listeners().begin();
         iter != lb->listeners().end(); ++iter) {
        const Loadbalancer::ListenerInfo &info = iter->second;
        const autogen::LoadbalancerListenerType &listener = info.properties;
        if (count) {
            ostr << "," << endl;
        }
        ostr << "      {" << endl
             << "         \"id\":\"" << iter->first << "\"," << endl
             << "         \"protocol\":\"" << listener.protocol << "\"," << endl
             << "         \"port\":" << listener.protocol_port << "," << endl
             << "         \"admin-state\":" << std::boolalpha
             << listener.admin_state << "," << endl;
        *out << ostr.str();
        ostr.str(std::string());
        GeneratePools(out, info.pools);
        *out << "      }";
        count++;
    }
    if (count) {
        *out << endl;
    }
    *out << "   ]" << endl;
}

void LoadbalancerConfig::GeneratePools
    (ostream *out, const Loadbalancer::PoolSet &pools) const {
    *out << "         \"pools\":[" << endl;
    int count = 0;
    for (Loadbalancer::PoolSet::const_iterator iter = pools.begin();
         iter != pools.end(); ++iter) {
        boost::uuids::uuid u = *iter;
        LoadbalancerPoolKey key(u);
        LoadbalancerPool *pool = static_cast<LoadbalancerPool *>
            (agent_->loadbalancer_pool_table()->FindActiveEntry(&key));
        if (!pool) {
            continue;
        }
        assert(pool->type() == LoadbalancerPool::LBAAS_V2);
        if (count) {
            *out << "," << endl;
        }
        *out << "            {" << endl;
        const LoadBalancerPoolInfo *info = pool->properties();
        GeneratePool(out, u, *info, "            ");
        GenerateMembers(out, *info, "            ");
        GenerateHealthMonitors(out, *info, "            ");
        GenerateCustomAttributes(out, *info, "            ");
        count++;
        *out << "            }";
    }
    if (count) {
        *out << endl;
    }
    *out << "         ]" << endl;
}

void LoadbalancerConfig::GenerateV2Config(const string &filename,
                                          Loadbalancer *lb) const {
    ofstream fs(filename.c_str());
    if (fs.fail()) {
        LOG(ERROR, "File create " << filename << ": " << strerror(errno));
    }

    fs << "{" << endl;
    fs << "   \"ssl-crt\":\"" << agent_->params()->si_lb_ssl_cert_path()
       << "\"," << endl;
    GenerateLoadbalancer(&fs, lb);
    GenerateListeners(&fs, lb);
    fs << "}" << endl;
    fs.close();
}
