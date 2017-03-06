#include "flow_token.h"
#include "flow_proto.h"

Token::Token(TokenPool *pool, TokenHolder *token_holder) :
    pool_(pool), token_holder_(token_holder) {
}

Token::~Token() {
    if (pool_)
        pool_->FreeToken();
    token_holder_ = NULL;
    LOG(ERROR, "Releasing token");
}

TokenPool::TokenPool(const std::string &name, Proto *proto,
                             int count) :
    name_(name), max_tokens_(count), min_tokens_(count),
    low_water_mark_((count * 10)/100), failures_(0), restarts_(0),
    proto_(proto) {
    token_count_ = count;
}

TokenPool::~TokenPool() {
}

void TokenPool::FreeToken() {
    int val = token_count_.fetch_and_increment();
    assert(val <= max_tokens_);
    if (val == low_water_mark_) {
        proto_->TokenAvailable(this);
    }
}

bool TokenPool::TokenCheck() const {
    if (token_count_ > 0) {
        return true;
    }

    failures_++;
    return false;
}

TokenPtr TokenPool::GetToken(TokenHolder *token_holder) {
    int val = token_count_.fetch_and_decrement();
    if (val < min_tokens_)
        min_tokens_ = val;
    return TokenPtr(new Token(this, token_holder));
}
