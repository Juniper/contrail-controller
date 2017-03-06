#ifndef __AGENT_PKT_FLOW_TOKEN_H__
#define __AGENT_PKT_FLOW_TOKEN_H__

#include <memory>
#include <tbb/atomic.h>
#include <base/util.h>

class Token;
class TokenPool;
class FlowEntry;
class Proto;

typedef boost::shared_ptr<Token> TokenPtr;

//Entity holding the token i.e flow and MAC learning
//entry for now
class TokenHolder {
public:
    TokenHolder() {}
    virtual ~TokenHolder() {}

private:
    DISALLOW_COPY_AND_ASSIGN(TokenHolder);
};

class Token {
public:
    Token(TokenPool *pool, TokenHolder *entry);
    virtual ~Token();

protected:
    TokenPool *pool_;
    TokenHolder *token_holder_;
    DISALLOW_COPY_AND_ASSIGN(Token);
};

class TokenPool {
public:
    TokenPool(const std::string &name, Proto *proto, int count);
    virtual ~TokenPool();

    virtual TokenPtr GetToken(TokenHolder *token_entry);
    int token_count() const { return token_count_; }
    bool TokenCheck() const;
    uint64_t failures() const { return failures_; }
    void IncrementRestarts() { restarts_++; }
    uint64_t restarts() const { return restarts_; }
protected:
    friend class Token;

    // Token destructor invokes this
    void FreeToken();

    std::string name_;
    int max_tokens_;
    int min_tokens_;
    int low_water_mark_;
    tbb::atomic<int> token_count_;
    mutable uint64_t failures_;
    uint64_t restarts_;
    Proto *proto_;
    DISALLOW_COPY_AND_ASSIGN(TokenPool);
};

class FlowTokenPool : public TokenPool {
public:
    FlowTokenPool(const std::string &name, Proto *proto, int count):
        TokenPool(name, proto, count) {}
    virtual ~FlowTokenPool() {}
};

class FlowToken : public Token {
    FlowToken(TokenPool *pool, TokenHolder *entry):
        Token(pool, entry) {}
    virtual ~FlowToken() {}
};
#endif //  __AGENT_PKT_FLOW_TOKEN_H__
