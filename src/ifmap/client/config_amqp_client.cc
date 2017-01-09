/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "config_amqp_client.h"

#include <stdio.h>

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include "rapidjson/document.h"

#include "base/task.h"
#include "ifmap/ifmap_config_options.h"
#include "config_client_manager.h"

using namespace std;
using namespace rapidjson;

bool ConfigAmqpClient::disable_;

class ConfigAmqpClient::RabbitMQReader : public Task {
public:
    RabbitMQReader(ConfigAmqpClient *amqpclient)
        : Task(amqpclient->reader_task_id()), amqpclient_(amqpclient) {
    }

    virtual bool Run();
    string Description() const { return "ConfigAmqpClient::RabbitMQReader"; }

private:
    ConfigAmqpClient *amqpclient_;
    AmqpClient::Channel::ptr_t channel_;
    string consumer_tag_;
    bool ConnectToRabbitMQ();
};

ConfigAmqpClient::ConfigAmqpClient(ConfigClientManager *mgr, string hostname,
                                   const IFMapConfigOptions &options) :
    mgr_(mgr), hostname_(hostname), rabbitmq_ip_(options.rabbitmq_ip),
    rabbitmq_port_(options.rabbitmq_port),
    rabbitmq_user_(options.rabbitmq_user),
    rabbitmq_password_(options.rabbitmq_password),
    rabbitmq_vhost_(options.rabbitmq_vhost),
    rabbitmq_use_ssl_(options.rabbitmq_use_ssl),
    rabbitmq_ssl_version_(options.rabbitmq_ssl_version),
    rabbitmq_ssl_keyfile_(options.rabbitmq_ssl_keyfile),
    rabbitmq_ssl_certfile_(options.rabbitmq_ssl_certfile),
    rabbitmq_ssl_ca_certs_(options.rabbitmq_ssl_ca_certs) {

    if (disable_)
        return;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    reader_task_id_ = scheduler->GetTaskId("amqp::RabbitMQReader");
    monitor_task_id_ = scheduler->GetTaskId("amqp::RabbitMQMonitor");
    Task *task = new RabbitMQReader(this);
    scheduler->Enqueue(task);
}

ConfigAmqpClient::~ConfigAmqpClient() {
}

void ConfigAmqpClient::EnqueueUUIDRequest(string uuid_str, string obj_type,
                                     string oper) {
    mgr_->EnqueueUUIDRequest(uuid_str, obj_type, oper);
}

string ConfigAmqpClient::FormAmqpUri() const {
    return string("amqp://" + rabbitmq_user() + ":" + rabbitmq_password() +
                  "@" + rabbitmq_ip() + ":" +  rabbitmq_port());
}

bool ConfigAmqpClient::RabbitMQReader::ConnectToRabbitMQ() {
    string uri = amqpclient_->FormAmqpUri();
    try {
        channel_ = AmqpClient::Channel::CreateFromUri(uri);
        // passive = false, durable = false
        channel_->DeclareExchange("vnc_config.object-update",
                                  AmqpClient::Channel::EXCHANGE_TYPE_FANOUT);
        string queue_name = string("control-node.") + amqpclient_->hostname();
        string queue = channel_->DeclareQueue(queue_name);
        channel_->BindQueue(queue, "vnc_config.object-update");
        consumer_tag_ = channel_->BasicConsume(queue);
    } catch (std::exception &e) {
        static std::string what = e.what();
        std::cout << "Caught fatal exception while connecting to RabbitMQ: " << what << std::endl;
        return false;
    } catch (...) {
        std::cout << "Caught fatal unknown exception while connecting to RabbitMQ: " << std::endl;
        assert(0);
    }
    return true;
}

bool ConfigAmqpClient::ProcessMessage(const string &json_message) {
    std::cout << "Rxed Message : " << json_message << std::endl;
    Document document;
    document.Parse<0>(json_message.c_str());

    if (document.HasParseError()) {
        size_t pos = document.GetErrorOffset();
        // GetParseError returns const char *
        std::cout << "Error in parsing JSON message from rabbitMQ at "
            << pos << "with error description"
            << document.GetParseError() << std::endl;
        return false;
    } else {
        std::cout << "Success " << std::endl;
        string oper = "";
        string uuid_str = "";
        string obj_type = "";
        for (Value::ConstMemberIterator itr = document.MemberBegin();
             itr != document.MemberEnd(); ++itr) {
            string key(itr->name.GetString());
            if (key == "oper") {
                oper = itr->value.GetString();
            } else if (key == "type") {
                obj_type = itr->value.GetString();
            } else if (key == "uuid") {
                uuid_str = itr->value.GetString();
            }
        }
        if ((oper == "") || (uuid_str == "") || (obj_type == "")) {
            assert(0);
        }
        EnqueueUUIDRequest(uuid_str, obj_type, oper);
    }
    return true;
}

bool ConfigAmqpClient::RabbitMQReader::Run() {
    while (true) {
        if (ConnectToRabbitMQ()) {
            break;
        }
    }
    AmqpClient::Envelope::ptr_t envelope;
    while (envelope = channel_->BasicConsumeMessage(consumer_tag_)) {
        amqpclient_->ProcessMessage(envelope->Message()->Body());
    }
    return true;
}

int ConfigAmqpClient::reader_task_id() const {
    return reader_task_id_;
}

int ConfigAmqpClient::monitor_task_id() const {
    return monitor_task_id_;
}

string ConfigAmqpClient::hostname() const {
    return hostname_;
}
