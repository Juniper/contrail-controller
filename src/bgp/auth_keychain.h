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
    typedef AuthenticationKeyChain::const_iterator const_iterator;
    typedef AuthenticationKeyChain::size_type AkcSz_t;
    iterator begin() { return auth_key_store_.begin(); }
    iterator end() { return auth_key_store_.end(); }
    const_iterator begin() const { return auth_key_store_.begin(); }
    const_iterator end() const { return auth_key_store_.end(); }

    AuthKeyChain();
    void Update(const AuthenticationKeyChain &input);
    const AuthenticationKey *Find(const std::string &input_key_id) const;
    bool Empty() const;
    void Clear();
    static bool AuthKeyIsMd5(const AuthenticationKey &item);
    static bool AuthKeysAreEqual(const AuthenticationKey &lhs,
                                 const AuthenticationKey &rhs);
    bool IsEqual(const AuthenticationKeyChain &input);

private:
    AuthenticationKeyChain auth_key_store_;
};

#endif  // #ifndef __AUTH_KEYCHAIN_H__
