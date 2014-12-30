#!/usr/bin/python
# -*- coding: utf-8 -*-

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright (c) 2015 Juniper Networks
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Numan Siddique, eNovance.


import urllib

def encode_string(enc_str, encoding='utf-8'):
    """Encode the string using urllib.quote_plus

    Eg. @input:
            enc_str = 'neté
            type - 'unicode' or 'str'
        @retval
            enc_str = 'net%C3%A9%C3%B9'
            type - str
    """
    try:
        enc_str.encode()
    except (UnicodeDecodeError, UnicodeEncodeError):
        if type(enc_str) is unicode:
            enc_str = enc_str.encode(encoding)
        enc_str = urllib.quote_plus(enc_str)
    except Exception:
        pass
    return enc_str


def decode_string(dec_str, encoding='utf-8'):
    """Decode the string previously encoded using urllib.quote_plus.

    Eg. If dec_str = 'net%C3%A9%C3%B9'
           type - 'unicode' or 'str'
        @retval
            ret_dec_str = 'neté
            type - unicode
    """
    ret_dec_str = dec_str
    try:
        if type(ret_dec_str) is unicode:
            ret_dec_str = str(ret_dec_str)
        ret_dec_str = urllib.unquote_plus(ret_dec_str)
        return ret_dec_str.decode(encoding)
    except Exception:
        return dec_str
