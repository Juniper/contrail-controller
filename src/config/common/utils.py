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
import errno
import urllib
from collections import OrderedDict
import sys
import cStringIO
import logging

from cfgm_common import vnc_cgitb


def cgitb_hook(info=None, **kwargs):
    vnc_cgitb.Hook(**kwargs).handle(info or sys.exc_info())
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
        # Check if bundle needs to be replaced if
        # constituent files were updated
        bundle_is_stale = False
        bundle_mod_time = os.path.getmtime(bundle)
        for cert in certs:
            if os.path.getmtime(cert) > bundle_mod_time:
                bundle_is_stale = True
                break
        if not bundle_is_stale:
            return bundle

    try:
        os.makedirs(os.path.dirname(bundle))
    except OSError as e:
        if e.errno != errno.EEXIST:
            raise
    with open(bundle, 'w') as ofile:
        for cert in certs:
            with open(cert) as ifile:
                for line in ifile:
                    ofile.write(line)
    os.chmod(bundle,0o777)
    return bundle
# end CreateCertKeyCaBundle

# <uuid> | "tenant-"<uuid> | "domain-"<uuid>
def shareinfo_from_perms2_tenant(field):
    x = field.split(":")
    if len(x) == 1:
        x.insert(0, "tenant")
    return x
# end

def shareinfo_from_perms2(field):
    x = field.split(":")
    if len(x) == 2:
        x.insert(0, "tenant")
    return x
# end

def compare_refs(old_refs, new_refs):
    # compare refs in an object
    old_ref_dict = dict((':'.join(ref['to']), ref.get('attr')) for ref in old_refs or [])
    new_ref_dict = dict((':'.join(ref['to']), ref.get('attr')) for ref in new_refs or [])
    return old_ref_dict == new_ref_dict
# end compare_refs
