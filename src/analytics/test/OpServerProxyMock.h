/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include "../OpServerProxy.h"

class OpServerProxyMock : public OpServerProxy {
public:
    EventManager *evm_;
    OpServerProxyMock(EventManager *evm) :
        OpServerProxy(),
        evm_(evm) {
    }
    ~OpServerProxyMock() {}
    
    MOCK_METHOD10(UVEUpdate, bool(const std::string &type, const std::string &attr,
                       const std::string &source, const std::string &module,
                       const std::string &key, const std::string &message, int32_t seq,
                       const std::string &agg, const std::string &atyp, int64_t ts));


    MOCK_METHOD4(UVESend, bool(const std::string &type, const std::string &source,
                const std::string &key, const std::string &message));

    bool 
    GetSeq(const std::string &source, const std::string &module, GetSeqReply gsr) {
        evm_->io_service()->post(boost::bind(&OpServerProxyMock::GetSeqCb, this, gsr));
        return true;
    }

    void 
    GetSeqCb(GetSeqReply gsr) {
        std::map<std::string,int32_t> dummy;
        (gsr)(dummy);
    }

    bool
    DeleteUVEs(const std::string &source, const std::string &module, DeleteUVEsReply dur) {
        evm_->io_service()->post(boost::bind(&OpServerProxyMock::DeleteUVEsCb, this, dur));
        return true;
    }

    void 
    DeleteUVEsCb(DeleteUVEsReply dur) {
        bool dummy = true;
        (dur)(dummy);        
    }

};
