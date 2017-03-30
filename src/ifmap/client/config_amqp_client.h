/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_config_amqp_client_h
#define ctrlplane_config_amqp_client_h

#include <string>
#include <vector>

#include <boost/asio/ip/tcp.hpp>

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <tbb/atomic.h>

struct IFMapConfigOptions;
class ConfigClientManager;
struct ConfigAmqpConnInfo;

// Interface to AmqpClient
class ConfigAmqpChannel {
public:
    ConfigAmqpChannel() { }
    virtual ~ConfigAmqpChannel() { }

    virtual AmqpClient::Channel::ptr_t CreateFromUri(std::string uri) {
        return (channel_ = AmqpClient::Channel::CreateFromUri(uri));
    }

    virtual void DeclareExchange(const std::string &exchange_name,
         const std::string &exchange_type, bool passive, bool durable,
         bool auto_delete) {
        channel_->DeclareExchange(exchange_name, exchange_type, passive,
                                  durable, auto_delete);
    }

    virtual void DeleteQueue(const std::string &queue_name, bool if_unused,
                             bool if_empty) {
        channel_->DeleteQueue(queue_name, if_unused, if_empty);
    }

    virtual std::string DeclareQueue(const std::string &queue_name,
                bool passive, bool durable, bool exclusive, bool auto_delete) {
        return channel_->DeclareQueue(queue_name, passive, durable, exclusive,
                                      auto_delete);
    }

    virtual void BindQueue(const std::string &queue_name,
                           const std::string &exchange_name,
                           const std::string &routing_key = "") {
        channel_->BindQueue(queue_name, exchange_name, routing_key);
    }

    virtual std::string BasicConsume(const std::string &queue,
                const std::string &consumer_tag, bool no_local, bool no_ack,
                bool exclusive, boost::uint16_t message_prefetch_count) {
        return channel_->BasicConsume(queue, consumer_tag, no_local, no_ack,
                                      exclusive, message_prefetch_count);
    }

    virtual bool BasicConsumeMessage(const std::string &consumer_tag,
         AmqpClient::Envelope::ptr_t &envelope, int timeout) {
        return channel_->BasicConsumeMessage(consumer_tag, envelope, timeout);
    }

    virtual void BasicAck(const AmqpClient::Envelope::ptr_t &message) {
        channel_->BasicAck(message);
    }

private:
    AmqpClient::Channel::ptr_t channel_;
};

/*
 * This is class interacts with RabbitMQ
 */
class ConfigAmqpClient {
public:
    ConfigAmqpClient(ConfigClientManager *mgr, std::string hostname,
                 std::string module_name, const IFMapConfigOptions &options);
    virtual ~ConfigAmqpClient() { }

    std::string rabbitmq_ip() const {
        if (current_server_index_ >= rabbitmq_ips_.size())
            return "";
        return rabbitmq_ips_[current_server_index_];
    }

    std::string rabbitmq_port() const {
        if (current_server_index_ >= rabbitmq_ips_.size())
            return "";
        return rabbitmq_ports_[current_server_index_];
    }

    std::string rabbitmq_user() const { return rabbitmq_user_; }
    std::string rabbitmq_password() const { return rabbitmq_password_; }
    std::string rabbitmq_vhost() const { return rabbitmq_vhost_; }
    bool rabbitmq_use_ssl() const { return rabbitmq_use_ssl_; }
    std::string rabbitmq_ssl_version() const { return rabbitmq_ssl_version_; }
    std::string rabbitmq_ssl_keyfile() const { return rabbitmq_ssl_keyfile_; }
    std::string rabbitmq_ssl_certfile() const { return rabbitmq_ssl_certfile_; }
    std::string rabbitmq_ssl_ca_certs() const { return rabbitmq_ssl_ca_certs_; }
    ConfigClientManager *config_manager() const { return mgr_; }
    ConfigClientManager *config_manager() { return mgr_; }
    boost::asio::ip::tcp::endpoint endpoint() const { return endpoint_; }
    int reader_task_id() const { return reader_task_id_; }
    std::string hostname() const { return hostname_; }
    std::string module_name() const { return module_name_; }

    static void set_disable(bool disable) { disable_ = disable; }

    std::string FormAmqpUri() const;
    void EnqueueUUIDRequest(std::string oper, std::string obj_type,
                       std::string uuid_str);
    bool ProcessMessage(const std::string &json_message);
    void set_connected(bool connected);
    void GetConnectionInfo(ConfigAmqpConnInfo &info) const;
    bool terminate() const { return terminate_; }
    void set_terminate(bool terminate) { terminate_ = terminate; }

private:
    // A Job for reading the rabbitmq
    class RabbitMQReader;

    ConfigClientManager *mgr_;
    std::string hostname_;
    std::string module_name_;

    int reader_task_id_;
    size_t current_server_index_;
    bool terminate_;
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
    boost::asio::ip::tcp::endpoint endpoint_;
    tbb::atomic<bool> connection_status_;
    tbb::atomic<uint64_t> connection_status_change_at_;
};

#endif // ctrlplane_config_amqp_client_h
