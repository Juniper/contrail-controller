/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_db_client.h"

#include <boost/tokenizer.hpp>

#include "base/string_util.h"
#include "ifmap/ifmap_config_options.h"

using namespace std;

ConfigDbClient::ConfigDbClient(const IFMapConfigOptions &options)
    : config_db_user_(options.config_db_username),
      config_db_password_(options.config_db_password) {

    for (vector<string>::const_iterator iter =
                options.config_db_server_list.begin();
         iter != options.config_db_server_list.end(); iter++) {

        string server_info(*iter);
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(server_info, sep);
        tokenizer::iterator tit = tokens.begin();
        string ip(*tit);
        config_db_ips_.push_back(ip);
        ++tit;
        string port_str(*tit);
        int port;
        stringToInteger(port_str, port);
        config_db_ports_.push_back(port);
    }
}

ConfigDbClient::~ConfigDbClient() {
}

string ConfigDbClient::config_db_user() const {
    return config_db_user_;
}

string ConfigDbClient::config_db_password() const {
    return config_db_password_;
}

vector<string> ConfigDbClient::config_db_ips() const {
    return config_db_ips_;
}

int ConfigDbClient::GetFirstConfigDbPort() const {
    return !config_db_ports_.empty() ? config_db_ports_[0] : 0;
}

