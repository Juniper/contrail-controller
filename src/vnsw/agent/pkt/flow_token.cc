#include "flow_token.h"
#include "flow_proto.h"

FlowToken::FlowToken(FlowTokenPool *pool, FlowEntry *flow) :
    flow_(flow), pool_(pool) {
}

FlowToken::~FlowToken() {
    if (pool_)
        pool_->FreeToken();
    flow_ = NULL;
}

FlowTokenPool::FlowTokenPool(const std::string &name, FlowProto *proto,
                             int count) :
    name_(name), max_tokens_(count), min_tokens_(count), failures_(0),
    proto_(proto) {
    token_count_ = count;
}

FlowTokenPool::~FlowTokenPool() {
}

FlowTokenPtr FlowTokenPool::GetToken(FlowEntry *flow) {
    int val = token_count_.fetch_and_decrement();
    if (val < min_tokens_)
        min_tokens_ = val;
    return FlowTokenPtr(new FlowToken(this, flow));
}

void FlowTokenPool::FreeToken() {
    int val = token_count_.fetch_and_increment();
    assert(val <= max_tokens_);
    if (val <= min_tokens_) {
        proto_->TokenAvailable(this);
    }
}

bool FlowTokenPool::TokenCheck() const {
    if (token_count_ > 0) {
        return true;
    }

    failures_++;
    return false;
}
