/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __AUTH_KEYCHAIN_H__
#define __AUTH_KEYCHAIN_H__

#include <string>

#include "bgp/bgp_config.h"

class AuthKeyChain {
public:
    typedef AuthenticationKeyChain::iterator iterator;
    typedef AuthenticationKeyChain::size_type AkcSz_t;
    iterator begin() { return auth_key_store_.begin(); }
    iterator end() { return auth_key_store_.end(); }

    AuthKeyChain();
    void Update(const AuthenticationKeyChain &input);
    AuthenticationKey *Find(const std::string &input_key_id);
    bool KeyPresent(const std::string &input_key_id);
    bool Empty();
    void Clear();
    bool AuthKeyIsMd5(const AuthenticationKey &item);
    bool AuthKeysAreEqual(const AuthenticationKey &lhs,
                          const AuthenticationKey &rhs);
    bool IsEqual(const AuthenticationKeyChain &input);

private:
    AuthenticationKeyChain auth_key_store_;
};

#endif  // #ifndef __AUTH_KEYCHAIN_H__
