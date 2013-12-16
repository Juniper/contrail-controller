/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "redis_sentinel_client.h"

RedisSentinelClient::RedisSentinelClient(
                        EventManager *evm, 
                        const std::string& sentinel_ip,
                        unsigned short sentinel_port,
                        RedisServices& services,
                        RedisMasterUpdateCb redis_master_update_cb) : 
        evm_(evm), 
        sentinel_ip_(sentinel_ip), 
        services_(services),
        redis_master_update_cb_(redis_master_update_cb),
        rx_msg_count_(0),
        rx_msg_cnt_since_last_conn_(0),
        sentinel_msg_cb_(NULL),
        getmaster_timer_(*evm->io_service()) {
    sentinel_conn_.reset(new RedisAsyncConnection(evm, sentinel_ip, sentinel_port, 
            boost::bind(&RedisSentinelClient::ConnUp, this),
            boost::bind(&RedisSentinelClient::ConnDown, this)));
    Connect();
}

RedisSentinelClient::~RedisSentinelClient() {
    boost::system::error_code ec;
    getmaster_timer_.cancel(ec);
}

void RedisSentinelClient::Connect() {
    sentinel_conn_.get()->RAC_Connect();
}

void RedisSentinelClient::ConnUp() {
    LOG(DEBUG, "Redis Sentinel Connection Up");
    evm_->io_service()->post(boost::bind(
                &RedisSentinelClient::ConnUpPostProcess, this));
}

void RedisSentinelClient::ConnUpPostProcess() {
    sentinel_msg_cb_ = boost::bind(&RedisSentinelClient::ProcessSentinelMsg,
                                   this, _1, _2, _3);
    sentinel_conn_.get()->SetClientAsyncCmdCb(sentinel_msg_cb_);
    GetRedisMasters();
    sentinel_conn_.get()->RedisAsyncCommand(NULL, "PSUBSCRIBE *");
}

void RedisSentinelClient::ConnDown() {
    LOG(DEBUG, "Redis Sentinel Connection Down");
    boost::system::error_code ec;
    getmaster_timer_.cancel(ec);
    rx_msg_cnt_since_last_conn_ = 0;
    evm_->io_service()->post(boost::bind(&RedisSentinelClient::Connect, this));
}

void RedisSentinelClient::GetRedisMasters() {
    RedisServices::const_iterator it;
    for (it = services_.begin(); it != services_.end(); ++it) {
        sentinel_conn_.get()->RedisAsyncCommand(NULL, 
                    "SENTINEL get-master-addr-by-name %s", (*it).c_str());
    }
}

void RedisSentinelClient::UpdateRedisMaster(const std::string& service,
                                            const std::string& redis_ip,
                                            unsigned short redis_port) {
    RedisMasterInfo redis_master(redis_ip, redis_port);
    RedisMasterMap::iterator it = redis_master_map_.find(service);
    if (it != redis_master_map_.end()) {
        if (it->second == redis_master) {
            LOG(DEBUG, "No change in Redis master for service " << service);
            return;
        } else {
            redis_master_map_.erase(it);
        }
    }
    LOG(DEBUG, "Update Redis master " << redis_ip << ":" << redis_port <<
        " for service " << service);
    redis_master_map_.insert(std::make_pair(service, redis_master));
    redis_master_update_cb_(service, redis_ip, redis_port);
}

void RedisSentinelClient::ProcessSentinelMsg(const redisAsyncContext *c, 
                                             void *msg, void *privdata) {
    redisReply *sentinel_msg = (redisReply*)msg;

    LOG(DEBUG, "Received message from Redis Sentinel");

    if (!sentinel_msg) {
        LOG(DEBUG, "NULL Sentinel message");
        return;
    }

    rx_msg_count_++;
    rx_msg_cnt_since_last_conn_++;

    if (sentinel_msg->type == REDIS_REPLY_ARRAY) {
        LOG(DEBUG, "Redis Sentinel Message: " << sentinel_msg->elements);
        for (int i = 0; i < (int)sentinel_msg->elements; i++) {
            if (sentinel_msg->element[i]->type == REDIS_REPLY_STRING) {
                LOG(DEBUG, "Element" << i << " == " << 
                    sentinel_msg->element[i]->str);
            } else {
                LOG(DEBUG, "Element" << i << " type == " << 
                    sentinel_msg->element[i]->type);
            }
        }
    } else {
        LOG(DEBUG, "Redis Sentinel message type == " << sentinel_msg->type);
        boost::system::error_code ec;
        getmaster_timer_.expires_from_now(boost::posix_time::seconds(RedisSentinelClient::kGetMasterTime), ec);
        getmaster_timer_.async_wait(boost::bind(&RedisSentinelClient::GetRedisMasters, this));
        return;
    }

    if (!strncmp(sentinel_msg->element[0]->str, "psubscribe", 
                 strlen("psubscribe"))) {
        // Ignore response to PSUBSCRIBE *
        return;
    }

    if (!strncmp(sentinel_msg->element[0]->str, "pmessage", 
                 strlen("pmessage"))) {
        if (sentinel_msg->elements != 4) {
            LOG(ERROR, "Unknown Sentinel message");
            return;
        }
        if (!strncmp(sentinel_msg->element[2]->str,
                     "-sdown", strlen("-sdown")) ||
            (!strncmp(sentinel_msg->element[2]->str,
                      "-odown", strlen("-odown")))) {
            std::vector<std::string> channel_msg;
            boost::split(channel_msg, sentinel_msg->element[3]->str,
                         boost::is_any_of(" "), boost::token_compress_on);
            if (!channel_msg.size()) {
                LOG(ERROR, "Failed to decode message " << 
                    sentinel_msg->element[2]->str << "from sentinel");
                return;
            }
            if (strncmp(channel_msg[0].c_str(), "master", strlen("master"))) {
                // Ignore state change of non-master
                return;
            }
            /*
             * the format of channels "-sdown" and "-odown" for redis master is
             * master <master-name> <ip> <port> 
             */
            if (channel_msg.size() != 4) {
                LOG(ERROR, "Failed to decode message " << 
                    sentinel_msg->element[2]->str << "from sentinel");
                return;
            }
            RedisServices::const_iterator it = 
                std::find(services_.begin(), services_.end(), 
                          channel_msg[1]);
            if (it == services_.end()) {
                LOG(DEBUG, "Received Redis Sentinel Message for unknown master " <<
                    channel_msg[1]);
                return;
            }
            LOG(INFO, "Redis Master Up for service " << channel_msg[1] <<
                " [" << channel_msg[2] << ":" << channel_msg[3] << "]");
            UpdateRedisMaster(*it, channel_msg[2], 
                              atoi(channel_msg[3].c_str()));
        } else if (!strncmp(sentinel_msg->element[2]->str,
                            "+switch-master", strlen("+switch-master")) ||
                   (!strncmp(sentinel_msg->element[2]->str, 
                             "+redirect-to-master",
                             strlen("+redirect-to-master")))) {
            /* 
             * the channel 'switch-master' and 'redirect-to-master' is 
             * of the format:
             *
             * <master-name> <old-ip> <old-port> <new-ip> <new-port>
             */
            std::vector<std::string> redis_master_info;
            boost::split(redis_master_info, sentinel_msg->element[3]->str,
                         boost::is_any_of(" "), boost::token_compress_on);
            if (redis_master_info.size() != 5) {
                LOG(ERROR, "Failed to decode Redis Sentinel message " <<
                    sentinel_msg->element[2]->str);
                return;
            }
            RedisServices::const_iterator it = 
                std::find(services_.begin(), services_.end(), 
                          redis_master_info[0]);
            if (it == services_.end()) {
                LOG(DEBUG, "Received Redis Sentinel Message for unknown master " <<
                    redis_master_info[0]);
                return;
            }
            LOG(INFO, "Redis Master change for service " << 
                redis_master_info[0] << " [" << redis_master_info[1] << ":" <<
                redis_master_info[2] << "] => [" << redis_master_info[3] <<
                ":" << redis_master_info[4] << "]");
            UpdateRedisMaster(*it, redis_master_info[3],
                              atoi(redis_master_info[4].c_str()));
        } 
    } else {
        // this is response to "SENTINEL get-master-addr-by-name"
        assert(rx_msg_cnt_since_last_conn_ <= services_.size());
        if (sentinel_msg->elements != 2) {
            LOG(ERROR, "Unknown Redis Sentinel message");
            return;
        }
        LOG(INFO, "Redis Master for service " << 
            services_[rx_msg_cnt_since_last_conn_-1] << " is " << 
            sentinel_msg->element[0]->str << ":" <<
            atoi(sentinel_msg->element[1]->str));
        UpdateRedisMaster(services_[rx_msg_cnt_since_last_conn_-1],
                          sentinel_msg->element[0]->str,
                          atoi(sentinel_msg->element[1]->str));
    }
}
