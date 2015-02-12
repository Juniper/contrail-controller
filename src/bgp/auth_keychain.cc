/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/auth_keychain.h"

AuthKeyChain::AuthKeyChain() {
}

void AuthKeyChain::Update(const AuthenticationKeyChain &input) {
    auth_key_store_ = input;
}

AuthenticationKey *AuthKeyChain::Find(const std::string &input_key_id) {
    AuthenticationKey *item;
    for (size_t i = 0; i < auth_key_store_.size(); ++i) {
        item = &auth_key_store_[i];
        if (item->id.compare(input_key_id) == 0) {
            return item;
        }
    }
    return NULL;
}

bool AuthKeyChain::KeyPresent(const std::string &input_key_id) {
    for (size_t i = 0; i < auth_key_store_.size(); ++i) {
        AuthenticationKey item = auth_key_store_[i];
        if (item.id.compare(input_key_id) == 0) {
            return true;
        }
    }
    return false;
}

bool AuthKeyChain::Empty() {
    return auth_key_store_.empty();
}

void AuthKeyChain::Clear() {
    auth_key_store_.clear();
}

bool AuthKeyChain::AuthKeyIsMd5(const AuthenticationKey &item) {
    return item.IsMd5();
}

bool AuthKeyChain::AuthKeysAreEqual(const AuthenticationKey &lhs,
                                    const AuthenticationKey &rhs) {
    if ((lhs.id.compare(rhs.id) == 0) &&
        (lhs.type == rhs.type) &&
        (lhs.value.compare(rhs.value) == 0) &&
        (lhs.start_time == rhs.start_time)) {
        return true;
    }
    return false;
}

bool AuthKeyChain::IsEqual(const AuthenticationKeyChain &input) {
    if (auth_key_store_.size() != input.size()) {
        return false;
    }

    for (size_t ocnt = 0; ocnt < input.size(); ++ocnt) {
        bool found = false;
        for (size_t icnt = 0; icnt < auth_key_store_.size(); ++icnt) {
            if (AuthKeysAreEqual(input[ocnt], auth_key_store_[icnt])) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

