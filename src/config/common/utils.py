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


import os
import re
import urllib
from collections import OrderedDict
import sys
import cgitb
import cStringIO
import logging


# Masking of password from openstack/common/log.py
_SANITIZE_KEYS = ['adminPass', 'admin_pass', 'password', 'admin_password']

# NOTE(ldbragst): Let's build a list of regex objects using the list of
# _SANITIZE_KEYS we already have. This way, we only have to add the new key
# to the list of _SANITIZE_KEYS and we can generate regular expressions
# for XML and JSON automatically.
_SANITIZE_PATTERNS = []
_FORMAT_PATTERNS = [r'(%(key)s\s*[=]\s*[\"\']).*?([\"\'])',
                    r'(<%(key)s>).*?(</%(key)s>)',
                    r'([\"\']%(key)s[\"\']\s*:\s*[\"\']).*?([\"\'])',
                    r'([\'"].*?%(key)s[\'"]\s*:\s*u?[\'"]).*?([\'"])']

for key in _SANITIZE_KEYS:
    for pattern in _FORMAT_PATTERNS:
        reg_ex = re.compile(pattern % {'key': key}, re.DOTALL)
        _SANITIZE_PATTERNS.append(reg_ex)


def mask_password(message, secret="***"):
    """Replace password with 'secret' in message.
    :param message: The string which includes security information.
    :param secret: value with which to replace passwords.
    :returns: The unicode value of message with the password fields masked.

    For example:

    >>> mask_password("'adminPass' : 'aaaaa'")
    "'adminPass' : '***'"
    >>> mask_password("'admin_pass' : 'aaaaa'")
    "'admin_pass' : '***'"
    >>> mask_password('"password" : "aaaaa"')
    '"password" : "***"'
    >>> mask_password("'original_password' : 'aaaaa'")
    "'original_password' : '***'"
    >>> mask_password("u'original_password' :   u'aaaaa'")
    "u'original_password' :   u'***'"
    """
    if not any(key in message for key in _SANITIZE_KEYS):
        return message

    secret = r'\g<1>' + secret + r'\g<2>'
    for pattern in _SANITIZE_PATTERNS:
        message = re.sub(pattern, secret, message)
    return message
# end mask_password


def cgitb_hook(info=None, **kwargs):
    buf = sys.stdout
    if 'file' in kwargs:
        buf = kwargs['file']

    local_buf = cStringIO.StringIO()
    kwargs['file'] = local_buf
    cgitb.Hook(**kwargs).handle(info or sys.exc_info())

    doc = local_buf.getvalue()
    local_buf.close()
    buf.write(mask_password(doc))
    buf.flush()
# end cgitb_hook


def detailed_traceback():
    buf = cStringIO.StringIO()
    cgitb_hook(format="text", file=buf)
    tb_txt = buf.getvalue()
    buf.close()
    return tb_txt
# end detailed_traceback


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


class CacheContainer(object):
    def __init__(self, size):
        self.container_size = size
        self.dictionary = OrderedDict()

    def __getitem__(self, key, default=None):
        value = self.dictionary[key]
        # item accessed - put it in the front
        del self.dictionary[key]
        self.dictionary[key] = value

        return value

    def __setitem__(self, key, value):
        self.dictionary[key] = value
        if len(self.dictionary.keys()) > self.container_size:
            # container is full, loose the least used item
            self.dictionary.popitem(last=False)

    def __contains__(self, key):
        return key in self.dictionary

    def __repr__(self):
        return str(self.dictionary)


def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name
# end CamelCase


def str_to_class(class_name, module_name):
    try:
        return reduce(getattr, class_name.split("."), sys.modules[module_name])
    except Exception as e:
        logger = logging.getLogger(module_name)
        logger.warn("Exception: %s", str(e))
        return None
# end str_to_class


def obj_type_to_vnc_class(obj_type, module_name):
    return str_to_class(CamelCase(obj_type), module_name)
# end obj_type_to_vnc_class


def getCertKeyCaBundle(bundle, certs):
    if os.path.isfile(bundle):
        return bundle
    with open(bundle, 'w') as ofile:
        for cert in certs:
            with open(cert) as ifile:
                for line in ifile:
                    ofile.write(line)
    os.chmod(bundle,0o777)
    return bundle
# end CreateCertKeyCaBundle
