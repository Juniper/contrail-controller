/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __CSO_CONFIG_H__
#define __CSO_CONFIG_H__


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "discovery/client/discovery_client.h"
#include "http/client/vncapi.h"
#include "parser_util.h"
#include "configdb_connection.h"

class Options;



class CsoHostnameRecord {
    public:
        explicit CsoHostnameRecord(std::string name, std::string cso_hostaddr, std::string cso_tenant,
        std::string cso_site, std::string cso_device):
            refreshed_(true), name_(name), cso_hostaddr_(cso_hostaddr), cso_tenant_(cso_tenant),
            cso_site_(cso_site), cso_device_(cso_device) {
        }
        bool operator==(const CsoHostnameRecord &rhs) {
            return rhs.IsMe(name_) &&  rhs.cso_hostaddr() == cso_hostaddr_ &&
            rhs.cso_tenant() == cso_tenant_ && rhs.cso_site() == cso_site_ &&
            rhs.cso_device() == cso_device_;
        }

        const std::string name() { return name_; }
        const std::string cso_hostaddr() const { return cso_hostaddr_; }
        const std::string cso_tenant() const { return cso_tenant_; }
        const std::string cso_site() const { return cso_site_; }
        const std::string cso_device() const { return cso_device_; }

        bool IsMe(std::string name) const { return name == name_; }
        void Update(Options *o, DiscoveryServiceClient *c);
        void Refresh(std::string name, std::string cso_hostaddr,
                     std::string cso_tenant, std::string cso_site,
                     std::string cso_device) {
             if (cso_hostaddr_ != cso_hostaddr)
                cso_hostaddr_ = cso_hostaddr;
             if (cso_tenant_ != cso_tenant)
                cso_tenant_ = cso_tenant;
             if (cso_site_ != cso_site)
                cso_site_ = cso_site;
             if (cso_device_ != cso_device)
                cso_device_ = cso_device;

            refreshed_ = true;
        }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            cso_hostaddr_;
        std::string            cso_tenant_;
        std::string            cso_site_;
        std::string            cso_device_;
};

typedef std::map<std::string, boost::shared_ptr<CsoHostnameRecord> > Chr_t;

class CsoConfig {
    public:
        CsoConfig(boost::shared_ptr<ConfigDBConnection> cfgdb_connection);
        ~CsoConfig();
        void AddCsoHostnameRecord(std::string name, std::string cso_hostaddr,
                                  std::string cso_tenant, std::string cso_site,
                                  std::string cso_device);
        void PollCsoConfig();
        boost::shared_ptr<CsoHostnameRecord> GetCsoHostnameRecord(std::string name);

    private:
        void ReadConfig();
        void CsoConfigHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        Chr_t hostname_records_;
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection_;
};


#endif
