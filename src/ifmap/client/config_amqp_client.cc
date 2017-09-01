/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "config_amqp_client.h"

#include <boost/algorithm/string/find.hpp>
#include <boost/lexical_cast.hpp>
#include <stdio.h>

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include "rapidjson/document.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "base/connection_info.h"
#include "base/task.h"
#include "base/string_util.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_server_show_types.h"
#include "config_cassandra_client.h"
#include "config_client_manager.h"
#include "config_db_client.h"

using namespace boost;
using namespace std;
using namespace contrail_rapidjson;

bool ConfigAmqpClient::disable_;

class ConfigAmqpClient::RabbitMQReader : public Task {
public:
    RabbitMQReader(ConfigAmqpClient *amqpclient) :
            Task(amqpclient->reader_task_id()), amqpclient_(amqpclient) {
        channel_.reset(IFMapFactory::Create<ConfigAmqpChannel>());

        // Connect to rabbit-mq asap so that notification messages over
        // rabbit mq are never missed (during bulk db sync which happens
        // soon afterwards.
        ConnectToRabbitMQ();
    }

    virtual bool Run();
    string Description() const { return "ConfigAmqpClient::RabbitMQReader"; }

private:
    ConfigAmqpClient *amqpclient_;
    boost::scoped_ptr<ConfigAmqpChannel> channel_;
    string consumer_tag_;
    void ConnectToRabbitMQ(bool queue_delete = true);
    bool AckRabbitMessages(AmqpClient::Envelope::ptr_t &envelop);
    bool ReceiveRabbitMessages(AmqpClient::Envelope::ptr_t &envelop);
};

ConfigAmqpClient::ConfigAmqpClient(ConfigClientManager *mgr, string hostname,
                      string module_name, const IFMapConfigOptions &options) :
    mgr_(mgr), hostname_(hostname), module_name_(module_name),
    current_server_index_(0), terminate_(false),
    rabbitmq_user_(options.rabbitmq_user),
    rabbitmq_password_(options.rabbitmq_password),
    rabbitmq_vhost_(options.rabbitmq_vhost),
    rabbitmq_use_ssl_(options.rabbitmq_use_ssl),
    rabbitmq_ssl_version_(options.rabbitmq_ssl_version),
    rabbitmq_ssl_keyfile_(options.rabbitmq_ssl_keyfile),
    rabbitmq_ssl_certfile_(options.rabbitmq_ssl_certfile),
    rabbitmq_ssl_ca_certs_(options.rabbitmq_ssl_ca_certs) {

    connection_status_ = false;
    connection_status_change_at_ = UTCTimestampUsec();

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    reader_task_id_ = scheduler->GetTaskId("amqp::RabbitMQReader");

    if (options.rabbitmq_server_list.empty())
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
        Endpoint curr_ep;
        int port = 0;
        stringToInteger(port_str, port);
        boost::system::error_code ec;
        curr_ep.address(boost::asio::ip::address::from_string(ip, ec));
        curr_ep.port(port);
        endpoints_.push_back(curr_ep);
    }
}

void ConfigAmqpClient::StartRabbitMQReader() {
    if (disable_)
        return;

    // If reinit is triggerred, Don't start the rabbitmq reader
    if (config_manager()->is_reinit_triggered())
        return;

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    Task *task = new RabbitMQReader(this);
    scheduler->Enqueue(task);
}

void ConfigAmqpClient::EnqueueUUIDRequest(string oper, string obj_type,
                                     string uuid_str) {
    mgr_->EnqueueUUIDRequest(oper, obj_type, uuid_str);
}

string ConfigAmqpClient::FormAmqpUri() const {
    string uri = string("amqp://" + rabbitmq_user() + ":" +
                        rabbitmq_password() + "@" + rabbitmq_ip() + ":" +
                        rabbitmq_port());
    if (!rabbitmq_vhost().empty()) {
        if (rabbitmq_vhost().compare("/") != 0) {
            uri += "/" + rabbitmq_vhost();
        }
    }
    return uri;
}

void ConfigAmqpClient::ReportRabbitMQConnectionStatus(bool connected) const {
    if (connected) {
        // Update connection info
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::DATABASE, "RabbitMQ",
            process::ConnectionStatus::UP,
            endpoints(), "RabbitMQ connection established");
    } else {
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::DATABASE, "RabbitMQ",
            process::ConnectionStatus::DOWN,
            endpoints(), "RabbitMQ connection down");
    }
}

void ConfigAmqpClient::RabbitMQReader::ConnectToRabbitMQ(bool queue_delete) {
    amqpclient_->ReportRabbitMQConnectionStatus(false);
    amqpclient_->set_connected(false);
    size_t count = 0;
    while (true) {
        // If we are signalled to stop, break now.
        if (amqpclient_->config_manager()->is_reinit_triggered())
            return;
        string uri = amqpclient_->FormAmqpUri();
        try {
            if (amqpclient_->rabbitmq_use_ssl()) {
                int port = boost::lexical_cast<int>(
                        amqpclient_->rabbitmq_port());

                channel_->CreateSecure(
                    amqpclient_->rabbitmq_ssl_ca_certs(),
                    amqpclient_->rabbitmq_ip(),
                    amqpclient_->rabbitmq_ssl_keyfile(),
                    amqpclient_->rabbitmq_ssl_certfile(),
                    port,
                    amqpclient_->rabbitmq_user(),
                    amqpclient_->rabbitmq_password(),
                    amqpclient_->rabbitmq_vhost());
            } else {
                channel_->CreateFromUri(uri);
            }
            // passive = false, durable = false, auto_delete = false
            channel_->DeclareExchange("vnc_config.object-update",
              AmqpClient::Channel::EXCHANGE_TYPE_FANOUT, false, false, false);
            string queue_name =
                amqpclient_->module_name() + "." + amqpclient_->hostname();

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
            static string what = e.what();
            cout << "Caught fatal exception while connecting to RabbitMQ: "
                 << amqpclient_->rabbitmq_ip() << ":"
                 << amqpclient_->rabbitmq_port() << " : " << what << endl;
            if (++count == amqpclient_->rabbitmq_server_list_len()) {
                count = 0;
                // Tried connecting to all given servers.. Now wait to reconnect
                sleep(5);
            }
            amqpclient_->increment_rabbitmq_server_index();
            continue;
        } catch (...) {
            cout << "Caught fatal unknown exception while connecting to " 
                 << "RabbitMQ: " << amqpclient_->rabbitmq_ip() << ":"
                 << amqpclient_->rabbitmq_port() << endl;
            assert(0);
        }

        amqpclient_->ReportRabbitMQConnectionStatus(true);
        amqpclient_->set_connected(true);
        break;
    }
}

void ConfigAmqpClient::set_connected(bool connected) {
    connection_status_ = connected;
    connection_status_change_at_ = UTCTimestampUsec();
}

void ConfigAmqpClient::GetConnectionInfo(ConfigAmqpConnInfo &conn_info) const {
    conn_info.connection_status = connection_status_;
    conn_info.connection_status_change_at = connection_status_change_at_;
    conn_info.url = FormAmqpUri();
}

bool ConfigAmqpClient::ProcessMessage(const string &json_message) {
    Document document;
    document.Parse<0>(json_message.c_str());

    if (document.HasParseError()) {
        size_t pos = document.GetErrorOffset();
        // GetParseError returns const char *
        cout << "Error in parsing JSON message from rabbitMQ at "
            << pos << "with error description"
            << document.GetParseError() << endl;
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
            } else if (key == "fq_name") {
                if (!itr->value.IsArray())
                    continue;
                ostringstream os;
                SizeType sz = itr->value.GetArray().Size();
                if (sz == 0)
                    continue;
                for (SizeType i = 0; i < sz-1; i++) {
                    os << itr->value[i].GetString() << ":";
                }
                os << itr->value[sz-1].GetString();
                obj_name = os.str();
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
                                                            obj_type, obj_name);
        } else if (oper == "DELETE") {
            config_manager()->config_db_client()->
                InvalidateFQNameCache(uuid_str);
        }

        CONFIG_CLIENT_TRACE(IFMapRabbitMQMsgTrace, oper, obj_type, uuid_str);
        EnqueueUUIDRequest(oper, obj_type, uuid_str);
    }
    return true;
}

bool ConfigAmqpClient::RabbitMQReader::ReceiveRabbitMessages(
                                     AmqpClient::Envelope::ptr_t &envelope) {
    try {
        // timeout = 10ms.. To handle SIGHUP on config changes
        // On reinit, config client manager will trigger the amqp client
        // to shutdown. Blocking wait without timeout will not allow this.
        channel_->BasicConsumeMessage(consumer_tag_, envelope, 10);
        return true;
    } catch (std::exception &e) {
        static string what = e.what();
        cout << "Caught fatal exception while receiving messages from RabbitMQ:"
             << " " << amqpclient_->rabbitmq_ip() << ":"
             << amqpclient_->rabbitmq_port() << " : " << what << endl;
        return false;
    } catch (...) {
        cout << "Caught fatal unknown exception while receiving "
             << "messages from RabbitMQ: " << amqpclient_->rabbitmq_ip() << ":"
             << amqpclient_->rabbitmq_port() << endl;
        assert(0);
    }
    return true;
}

bool ConfigAmqpClient::RabbitMQReader::AckRabbitMessages(
                                     AmqpClient::Envelope::ptr_t &envelope) {
    try {
        channel_->BasicAck(envelope);
    } catch (std::exception &e) {
        static string what = e.what();
        cout << "Caught fatal exception while Acking message to RabbitMQ: "
             << amqpclient_->rabbitmq_ip() << ":"
             << amqpclient_->rabbitmq_port() << " : " << what << endl;
        return false;
    } catch (...) {
        cout << "Caught fatal unknown exception while acking messages from "
             << "RabbitMQ: " << amqpclient_->rabbitmq_ip() << ":"
             << amqpclient_->rabbitmq_port() << endl;
        assert(0);
    }
    return true;
}


bool ConfigAmqpClient::RabbitMQReader::Run() {
    // If reinit is triggerred, don't wait for end of config trigger
    // return from here to process reinit
    if (amqpclient_->config_manager()->is_reinit_triggered())
        return true;

    // To start consuming the message, we should have finised bulk sync
    amqpclient_->config_manager()->WaitForEndOfConfig();

    while (true) {
        // Test only
        if (amqpclient_->terminate())
            break;
        // If reinit is triggerred, break from the message receiving loop
        if (amqpclient_->config_manager()->is_reinit_triggered())
            break;
        AmqpClient::Envelope::ptr_t envelope;
        if (ReceiveRabbitMessages(envelope) == false) {
            ConnectToRabbitMQ(false);
            continue;
        }

        if (!envelope)
            continue;

        amqpclient_->ProcessMessage(envelope->Message()->Body());
        if (AckRabbitMessages(envelope) == false) {
            ConnectToRabbitMQ(false);
            continue;
        }
    }
    return true;
}
