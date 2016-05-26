/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __IFMAP_CONFIG_OPTIONS_H__
#define __IFMAP_CONFIG_OPTIONS_H__

struct IFMapConfigOptions {
    IFMapConfigOptions() :
        stale_entries_cleanup_timeout(0), end_of_rib_timeout(0),
        peer_response_wait_time(0) {
    }
    IFMapConfigOptions(const std::string &in_server,
            const std::string &in_password, const std::string &in_user,
            const std::string &in_certs_store, int in_sect_time,
            int in_eort_time, int in_prwt_time)
        : server_url(in_server), password(in_password), user(in_user),
          certs_store(in_certs_store),
          stale_entries_cleanup_timeout(in_sect_time),
          end_of_rib_timeout(in_eort_time),
          peer_response_wait_time(in_prwt_time) {
    }
    std::string server_url;
    std::string password;
    std::string user;
    std::string certs_store;
    int stale_entries_cleanup_timeout; // in seconds
    int end_of_rib_timeout; // in seconds
    int peer_response_wait_time; // in seconds
};

#endif /* defined(__IFMAP_CONFIG_OPTIONS_H__) */
