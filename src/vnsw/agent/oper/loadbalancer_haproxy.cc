/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "loadbalancer_haproxy.h"

#include <cerrno>
#include <fstream>
#include <boost/assign/list_of.hpp>
#include "base/logging.h"

#include "loadbalancer_properties.h"

using namespace std;
using boost::assign::map_list_of;

LoadbalancerHaproxy::LoadbalancerHaproxy()
        : protocol_default_("tcp"),
          balance_default_("roundrobin") {
    protocol_map_ = map_list_of
            ("TCP", "tcp")
            ("HTTP", "http")
            ("HTTPS", "tcp");
    balance_map_ = map_list_of
            ("ROUND_ROBIN", "roundrobin")
            ("LEAST_CONNECTIONS", "leastconn")
            ("SOURCE_IP", "source");
}

LoadbalancerHaproxy::~LoadbalancerHaproxy() {
}

const string &LoadbalancerHaproxy::ProtocolMap(const string &proto) const {
    map<string, string>::const_iterator loc = protocol_map_.find(proto);
    if (loc == protocol_map_.end()) {
        return protocol_default_;
    }
    return loc->second;
}

const string &LoadbalancerHaproxy::BalanceMap(const string &proto) const {
    map<string, string>::const_iterator loc = balance_map_.find(proto);
    if (loc == balance_map_.end()) {
        return balance_default_;
    }
    return loc->second;
}

/*
 * global
 *     daemon
 *     user nobody
 *     group nogroup
 *     log /dev/log local0
 *     log /dev/log local1 notice
 *     stats socket <path> mode 0666 level user
 */
void LoadbalancerHaproxy::GenerateGlobal(
    ostream *out, const LoadbalancerProperties &props) const {

    *out << "global" << endl;
    *out << string(4, ' ') << "daemon" << endl;
    *out << string(4, ' ') << "user nobody" << endl;
    *out << string(4, ' ') << "group nogroup" << endl;
    *out << endl;
}

void LoadbalancerHaproxy::GenerateDefaults(
    ostream *out, const LoadbalancerProperties &props) const {

    *out << "defaults" << endl;
    *out << string(4, ' ') << "log global" << endl;
    *out << string(4, ' ') << "retries 3" << endl;
    *out << string(4, ' ') << "option redispatch" << endl;
    *out << string(4, ' ') << "timeout connect 5000" << endl;
    *out << string(4, ' ') << "timeout client 50000" << endl;
    *out << string(4, ' ') << "timeout server 50000" << endl;
    *out << endl;
}

/*
 * frontend vip_id
 *     bind address:port
 *     mode [http|tcp]
 *     default_backend pool_id
 */
void LoadbalancerHaproxy::GenerateFrontend(
    ostream *out, const boost::uuids::uuid &pool_id,
    const LoadbalancerProperties &props) const {

    *out << "frontend " << props.vip_uuid() << endl;
    const autogen::VirtualIpType &vip = props.vip_properties();
    *out << string(4, ' ')
         << "bind " << vip.address << ":" << vip.protocol_port << endl;
    *out << string(4, ' ')
         << "mode " << ProtocolMap(vip.protocol) << endl;
    *out << string(4, ' ')
         << "default_backend " << pool_id << endl;

    if (vip.connection_limit >= 0) {
        *out << string(4, ' ')
             << "maxconn " << vip.connection_limit << endl;
    }
    *out << endl;
}


/*
 * backend <pool_id>
 *     mode <protocol>
 *     balance <lb_method>
 *     server <id> <address>:<port> weight <weight>
 */
void LoadbalancerHaproxy::GenerateBackend(
    ostream *out, const boost::uuids::uuid &pool_id,
    const LoadbalancerProperties &props) const {
    const autogen::LoadbalancerPoolType &pool = props.pool_properties();
    *out << "backend " << pool_id << endl;
    *out << string(4, ' ')
         << "mode " << ProtocolMap(pool.protocol) << endl;
    *out << string(4, ' ')
         << "balance " << BalanceMap(pool.loadbalancer_method) << endl;

    for (LoadbalancerProperties::MemberMap::const_iterator iter =
                 props.members().begin();
         iter != props.members().end(); ++iter) {
        const autogen::LoadbalancerMemberType &member = iter->second;
        *out << string(4, ' ')
             << "server " << iter->first << " " << member.address
             << ":" << member.protocol_port
             << " weight " << member.weight;
        *out << endl;
    }
}

void LoadbalancerHaproxy::GenerateConfig(
    const string &filename, const boost::uuids::uuid &pool_id,
    const LoadbalancerProperties &props) const {
    ofstream fs(filename.c_str());
    if (fs.fail()) {
        LOG(ERROR, "File create " << filename << ": " << strerror(errno));
    }

    GenerateGlobal(&fs, props);
    GenerateDefaults(&fs, props);
    GenerateFrontend(&fs, pool_id, props);
    GenerateBackend(&fs, pool_id, props);
    fs.close();
}
