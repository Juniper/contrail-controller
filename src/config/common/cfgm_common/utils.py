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


from __future__ import unicode_literals
from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import object
import urllib.request, urllib.parse, urllib.error
from collections import OrderedDict
import sys
from six import StringIO
from six.moves.configparser import NoOptionError

from cfgm_common import vnc_cgitb
from vnc_api.utils import (
    CamelCase, str_to_class, obj_type_to_vnc_class, getCertKeyCaBundle)


_DEFAULT_USER_DOMAIN_NAME = 'Default'
_DEFAULT_DOMAIN_ID = 'default'
_DEFAULT_ZK_COUNTER_PATH_PREFIX = '/vnc_api_server_obj_create/'
_DEFAULT_ZK_LOCK_PATH_PREFIX = '/vnc_api_server_locks/'
_DEFAULT_ZK_LOCK_TIMEOUT = 120


def cgitb_hook(info=None, **kwargs):
    vnc_cgitb.Hook(**kwargs).handle(info or sys.exc_info())
# end cgitb_hook


def detailed_traceback():
    buf = StringIO()
    cgitb_hook(format="text", file=buf)
    tb_txt = buf.getvalue()
    buf.close()
    return tb_txt
# end detailed_traceback


def encode_string(string, encoding='utf-8', safe=': ='):
    """Encode the string using urllib.quote_plus.

    Eg. @input:
            enc_stringstr = 'neté '
            type - 'unicode' or 'str'
        @retval
            enc_str = 'net%C3%A9+'
            type - str (newstr in python 2)
    """
    return urllib.parse.quote_plus(string, encoding=encoding, safe=safe)


def decode_string(string, encoding='utf-8'):
    """Decode the string previously encoded using urllib.unquote_plus.

    Eg. If string = 'net%C3%A9+'
            type - 'unicode' or 'str'
        @retval
            ret_dec_str = 'neté
            type - unicode (str in python 3)
    """
    return urllib.parse.unquote_plus(string, encoding=encoding)


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
        if len(list(self.dictionary.keys())) > self.container_size:
            # container is full, loose the least used item
            self.dictionary.popitem(last=False)

    def __contains__(self, key):
        return key in self.dictionary

    def __repr__(self):
        return str(self.dictionary)


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
    old_ref_dict = dict(
        (':'.join(ref['to']), ref.get('attr')) for ref in old_refs or [])
    new_ref_dict = dict(
        (':'.join(ref['to']), ref.get('attr')) for ref in new_refs or [])
    return old_ref_dict == new_ref_dict
# end compare_refs


def get_arg(args, name, default=None):
    try:
        kwarg = {name: eval('args.%s' % name)}
    except AttributeError:
        try:
            kwarg = {name: args.get('KEYSTONE', name)}
        except (NoOptionError, AttributeError):
            kwarg = {name: default}

    return kwarg
# end get_arg


def get_user_domain_kwargs(args):
    user_domain = get_arg(args, 'user_domain_id')
    if not user_domain.get('user_domain_id'):
        user_domain = get_arg(args, 'user_domain_name',
                              _DEFAULT_USER_DOMAIN_NAME)

    return user_domain
# end get_user_domain_kwargs


def get_project_scope_kwargs(args):
    scope_kwargs = {}
    project_domain_name = get_arg(args, 'project_domain_name')
    project_domain_id = get_arg(args, 'project_domain_id')
    if project_domain_name.get('project_domain_name'):
        # use project domain name
        scope_kwargs.update(**project_domain_name)
    elif project_domain_id.get('project_domain_id'):
        # use project domain id
        scope_kwargs.update(**project_domain_id)
    if scope_kwargs:
        admin_tenant_name = get_arg(args, 'admin_tenant_name')[
            'admin_tenant_name']
        project_name = get_arg(args, 'project_name', admin_tenant_name)
        scope_kwargs.update(project_name)
    return scope_kwargs
# end get_project_scope_kwargs


def get_domain_scope_kwargs(args):
    scope_kwargs = {}
    domain_name = get_arg(args, 'domain_name')
    domain_id = get_arg(args, 'domain_id', _DEFAULT_DOMAIN_ID)
    if domain_name.get('domain_name'):
        # use domain name
        scope_kwargs.update(**domain_name)
    elif domain_id.get('domain_id'):
        # use domain id
        scope_kwargs.update(**domain_id)
    return scope_kwargs
# end get_domain_scope_kwargs


def find_buildroot(path):
    try:
        return os.environ['BUILDTOP']
    except:
        return path + '/build/debug'
# end find_buildroot
