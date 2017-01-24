/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_amqp_client_h
#define ctrlplane_config_amqp_client_h

#include <string>
#include <vector>

struct IFMapConfigOptions;
class ConfigClientManager;

/*
 * This is class interacts with RabbitMQ
 */
class ConfigAmqpClient {
public:
    ConfigAmqpClient(ConfigClientManager *mgr, std::string hostname,
                     const IFMapConfigOptions &options);
    virtual ~ConfigAmqpClient();

    std::string rabbitmq_ip() const {
        return rabbitmq_ips_[current_server_index_];
    }

    std::string rabbitmq_port() const {
        return rabbitmq_ports_[current_server_index_];
    }

    std::string rabbitmq_user() const {
        return rabbitmq_user_;
    }

    std::string rabbitmq_password() const {
        return rabbitmq_password_;
    }

    std::string rabbitmq_vhost() const {
        return rabbitmq_vhost_;
    }

    bool rabbitmq_use_ssl() const {
        return rabbitmq_use_ssl_;
    }

    std::string rabbitmq_ssl_version() const {
        return rabbitmq_ssl_version_;
    }

    std::string rabbitmq_ssl_keyfile() const {
        return rabbitmq_ssl_keyfile_;
    }

    std::string rabbitmq_ssl_certfile() const {
        return rabbitmq_ssl_certfile_;
    }

    std::string rabbitmq_ssl_ca_certs() const {
        return rabbitmq_ssl_ca_certs_;
    }

    int reader_task_id() const;

    std::string FormAmqpUri() const;
    std::string hostname() const;

    void EnqueueUUIDRequest(std::string oper, std::string obj_type,
                       std::string uuid_str);
    bool ProcessMessage(const std::string &json_message);
    static void set_disable(bool disable) { disable_ = disable; }

    ConfigClientManager *config_manager() const {
        return mgr_;
    }
    ConfigClientManager *config_manager() {
        return mgr_;
    }
private:
    // A Job for reading the rabbitmq
    class RabbitMQReader;

    ConfigClientManager *mgr_;
    std::string hostname_;

    int reader_task_id_;
    int current_server_index_;
    std::vector<std::string> rabbitmq_ips_;
    std::vector<std::string> rabbitmq_ports_;
    std::string rabbitmq_user_;
    std::string rabbitmq_password_;
    std::string rabbitmq_vhost_;
    bool rabbitmq_use_ssl_;
    std::string rabbitmq_ssl_version_;
    std::string rabbitmq_ssl_keyfile_;
    std::string rabbitmq_ssl_certfile_;
    std::string rabbitmq_ssl_ca_certs_;
    static bool disable_;
};

#endif // ctrlplane_config_amqp_client_h
