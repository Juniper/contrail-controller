/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "config_amqp_client.h"

#include <boost/algorithm/string/find.hpp>
#include <stdio.h>

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include "rapidjson/document.h"

#include "base/string_util.h"
#include "base/task.h"
#include "ifmap/ifmap_config_options.h"
#include "config_cassandra_client.h"
#include "config_client_manager.h"
#include "config_db_client.h"

using namespace boost;
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
    bool ConnectToRabbitMQ(bool queue_delete = true);
    bool AckRabbitMessages(AmqpClient::Envelope::ptr_t &envelop);
    bool ReceiveRabbitMessages(AmqpClient::Envelope::ptr_t &envelop);
};

ConfigAmqpClient::ConfigAmqpClient(ConfigClientManager *mgr, string hostname,
                                   const IFMapConfigOptions &options) :
    mgr_(mgr), hostname_(hostname), current_server_index_(0),
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

    for (vector<string>::const_iterator iter =
                options.rabbitmq_server_list.begin();
         iter != options.rabbitmq_server_list.end(); iter++) {
        string server_info(*iter);
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(server_info, sep);
        tokenizer::iterator tit = tokens.begin();
        string ip(*tit);
        rabbitmq_ips_.push_back(ip);
        ++tit;
        string port_str(*tit);
        rabbitmq_ports_.push_back(port_str);
    }

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    reader_task_id_ = scheduler->GetTaskId("amqp::RabbitMQReader");
    Task *task = new RabbitMQReader(this);
    scheduler->Enqueue(task);
}

ConfigAmqpClient::~ConfigAmqpClient() {
}

void ConfigAmqpClient::EnqueueUUIDRequest(string oper, string obj_type,
                                     string uuid_str) {
    mgr_->EnqueueUUIDRequest(oper, obj_type, uuid_str);
}

string ConfigAmqpClient::FormAmqpUri() const {
    string uri = string("amqp://" + rabbitmq_user() + ":" + rabbitmq_password() +
      "@" + rabbitmq_ip() + ":" +  rabbitmq_port());
    if (rabbitmq_vhost() != "") {
        uri += "/" + rabbitmq_vhost();
    }
    return uri;
}

bool ConfigAmqpClient::RabbitMQReader::ConnectToRabbitMQ(bool queue_delete) {
    while (true) {
        string uri = amqpclient_->FormAmqpUri();
        try {
            channel_ = AmqpClient::Channel::CreateFromUri(uri);
            // passive = false, durable = false, auto_delete = false
            channel_->DeclareExchange("vnc_config.object-update",
              AmqpClient::Channel::EXCHANGE_TYPE_FANOUT, false, false, false);
            string queue_name =
                string("control-node.") + amqpclient_->hostname();

            if (queue_delete) {
                channel_->DeleteQueue(queue_name, true, true);
            }

            // passive = false, durable = false,
            // exclusive = true, auto_delete = false
            string queue = channel_->DeclareQueue(queue_name, false, false,
                                                  true, false);
            channel_->BindQueue(queue, "vnc_config.object-update");
            // no_local = true, no_ack = false,
            // exclusive = true, message_prefetch_count = 0
            consumer_tag_ = channel_->BasicConsume(queue, queue_name,
                                                   true, false, true, 0);
        } catch (std::exception &e) {
            static std::string what = e.what();
            std::cout << "Caught fatal exception while connecting to RabbitMQ: "
                << what << std::endl;
            // Wait to reconnect
            sleep(5);
            continue;
        } catch (...) {
            std::cout << "Caught fatal unknown exception while connecting to "
                << "RabbitMQ: " << std::endl;
            assert(0);
        }
        return true;
    }
    return false;
}

bool ConfigAmqpClient::ProcessMessage(const string &json_message) {
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
        string oper = "";
        string uuid_str = "";
        string obj_type = "";
        string obj_name = "";
        for (Value::ConstMemberIterator itr = document.MemberBegin();
             itr != document.MemberEnd(); ++itr) {
            string key(itr->name.GetString());
            if (key == "oper") {
                oper = itr->value.GetString();
            } else if (key == "type") {
                obj_type = itr->value.GetString();
            } else if (key == "imid") {
                string temp_imid = itr->value.GetString();
                iterator_range<string::iterator> r = find_nth(temp_imid, ":", 1);
                if (r.empty()) {
                    std::cout << "Failed to get fetch name from ampq message"
                        << std::endl;
                    continue;
                }
                obj_name =
                    temp_imid.substr(distance(temp_imid.begin(), r.begin())+1);
            } else if (key == "uuid") {
                uuid_str = itr->value.GetString();
            }
        }
        if ((oper == "") || (uuid_str == "") || (obj_type == "")) {
            assert(0);
        }
        if (oper == "CREATE") {
            assert(obj_name != "");
            config_manager()->config_db_client()->AddFQNameCache(uuid_str,
                                                                 obj_name);
        }
        EnqueueUUIDRequest(oper, obj_type, uuid_str);
    }
    return true;
}

bool ConfigAmqpClient::RabbitMQReader::ReceiveRabbitMessages(
                                     AmqpClient::Envelope::ptr_t &envelope) {
    try {
        // timeout = -1.. wait forever
        return (channel_->BasicConsumeMessage(consumer_tag_, envelope, -1));
    } catch (std::exception &e) {
        static std::string what = e.what();
        std::cout << "Caught fatal exception while receiving " <<
            "messages from RabbitMQ: " << what << std::endl;
        return false;
    } catch (...) {
        std::cout << "Caught fatal unknown exception while receiving " <<
            "messages from RabbitMQ: " << std::endl;
        assert(0);
    }
    return true;
}

bool ConfigAmqpClient::RabbitMQReader::AckRabbitMessages(
                                     AmqpClient::Envelope::ptr_t &envelope) {
    try {
        channel_->BasicAck(envelope);
    } catch (std::exception &e) {
        static std::string what = e.what();
        std::cout << "Caught fatal exception while Acking message to RabbitMQ: "
            << what << std::endl;
        return false;
    } catch (...) {
        std::cout << "Caught fatal unknown exception while acking messages " <<
            "from RabbitMQ: " << std::endl;
        assert(0);
    }
    return true;
}


bool ConfigAmqpClient::RabbitMQReader::Run() {
    assert(ConnectToRabbitMQ());
    while (true) {
        AmqpClient::Envelope::ptr_t envelope;
        if (ReceiveRabbitMessages(envelope) == false) {
            assert(ConnectToRabbitMQ(false));
            continue;
        }
        amqpclient_->ProcessMessage(envelope->Message()->Body());
        if (AckRabbitMessages(envelope) == false) {
            assert(ConnectToRabbitMQ(false));
            continue;
        }
    }
    return true;
}

int ConfigAmqpClient::reader_task_id() const {
    return reader_task_id_;
}

string ConfigAmqpClient::hostname() const {
    return hostname_;
}
