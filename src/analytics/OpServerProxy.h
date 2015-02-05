/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __OPSERVERPROXY_H__
#define __OPSERVERPROXY_H__

#include <string>
#include "io/event_manager.h"
#include <sandesh/sandesh.h>
#include "redis_types.h"

// This class can be used to send UVE Traces from vizd to the OpSever(s)
// Currently, this is done via Redis. 
class VizCollector;

class OpServerProxy {
public:

    enum CmdStatus {
        STATUS_INVALID = 0,
        STATUS_ERROR = 1,
        STATUS_NOTPRESENT = 2,
        STATUS_PRESENT = 3,
    };

    // To construct this interface, pass in the hostname and port for Redis
    OpServerProxy(EventManager *evm, VizCollector *collector,
            const std::string& redis_uve_ip, unsigned short redis_uve_port,
            const std::string& redis_uve_password,
            const std::string& brokers,
            uint16_t partitions);
    OpServerProxy() : impl_(NULL) { }
    virtual ~OpServerProxy();

    virtual bool UVEUpdate(const std::string &type, const std::string &attr,
                           const std::string &source, const std::string &node_type,
                           const std::string &module, 
                           const std::string &instance_id,
                           const std::string &key, const std::string &message,
                           int32_t seq, const std::string& agg, 
                           const std::string& atyp, int64_t ts);

    virtual bool UVENotif(const std::string &type,
                           const std::string &source, const std::string &node_type,
                           const std::string &module, 
                           const std::string &instance_id,
                           const std::string &key);

    // Use this to delete the object when the deleted attribute is set
    virtual bool UVEDelete(const std::string &type,
                       const std::string &source, const std::string &node_type,
                       const std::string &module, const std::string &instance_id,
                       const std::string &key, int32_t seq);

    virtual bool GetSeq(const std::string &source, const std::string &node_type,
        const std::string &module, const std::string &instance_id,
        std::map<std::string,int32_t> & seqReply);

    virtual bool DeleteUVEs(const std::string &source, const std::string &node_type,
                            const std::string &module, 
                            const std::string &instance_id);
    
    void FillRedisUVEInfo(RedisUveInfo& redis_uve_info);
private:
    class OpServerImpl;
    OpServerImpl *impl_;
};
#endif
