/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_client_manager_h
#define ctrlplane_config_client_manager_h

#include <boost/scoped_ptr.hpp>

#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_origin.h"

struct AutogenProperty;
class ConfigAmqpClient;
class ConfigDbClient;
class ConfigJsonParser;
struct DBRequest;
class EventManager;
class IFMapServer;
struct IFMapConfigOptions;

/*
 * This class is the manager that over-sees the retrieval of user configuration.
 * It interacts with the rabbit-mq client, the database-client and the parser
 * that parses the configuration received from the database-client. Its the
 * coordinator between these 3 pieces.
 */
class ConfigClientManager {
public:
    typedef std::list<struct DBRequest *> RequestList;
    ConfigClientManager(EventManager *evm, IFMapServer *ifmap_server,
                        std::string hostname,
                        const IFMapConfigOptions& config_options);
    void Initialize();
    ConfigAmqpClient *config_amqp_client() const;
    ConfigDbClient *config_db_client() const;
    ConfigJsonParser *config_json_parser() const;
    bool GetEndOfRibComputed() const;
    void EnqueueUUIDRequest(std::string uuid_str, std::string obj_type,
                            std::string oper);
    ConfigAmqpClient *config_amqp_client() { return config_amqp_client_.get(); }
    ConfigJsonParser *config_json_parser() { return config_json_parser_.get(); }
    ConfigDbClient *config_db_client() { return config_db_client_.get(); }

    void InsertRequestIntoQ(IFMapOrigin::Origin origin,
        const std::string &neigh_type, const std::string &neigh_name,
        const std::string &metaname, std::auto_ptr<AutogenProperty > pvalue,
        const IFMapTable::RequestKey &key, bool add_change,
        RequestList *req_list) const;
    void EnqueueListToTables(RequestList *req_list) const;

    std::string GetLinkName(const std::string &left,
                            const std::string &right) const;
    bool IsLinkWithAttr(const std::string &left,
                        const std::string &right) const;

    std::string GetWrapperFieldName(const std::string &type_name,
                                    const std::string &property_name) const;

private:
    typedef std::pair<std::string, std::string> LinkMemberPair;
    typedef std::pair<std::string, bool> LinkDataPair;
    typedef std::map<LinkMemberPair, LinkDataPair> LinkNameMap;
    typedef std::map<std::string, std::string> WrapperFieldMap;

    IFMapTable::RequestKey *CloneKey(const IFMapTable::RequestKey &src) const;

    LinkNameMap link_name_map_;
    EventManager *evm_;
    IFMapServer *ifmap_server_;
    boost::scoped_ptr<ConfigJsonParser> config_json_parser_;
    boost::scoped_ptr<ConfigDbClient> config_db_client_;
    boost::scoped_ptr<ConfigAmqpClient> config_amqp_client_;
    int thread_count_;
    WrapperFieldMap wrapper_field_map_;
};

#endif // ctrlplane_config_client_manager_h
