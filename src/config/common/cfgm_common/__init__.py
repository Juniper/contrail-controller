#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
import sys
import uuid

IP_FABRIC_VN_FQ_NAME = ['default-domain', 'default-project', 'ip-fabric']
IP_FABRIC_RI_FQ_NAME = IP_FABRIC_VN_FQ_NAME + ['__default__']
LINK_LOCAL_VN_FQ_NAME = ['default-domain', 'default-project', '__link_local__']
LINK_LOCAL_RI_FQ_NAME = LINK_LOCAL_VN_FQ_NAME + ['__link_local__']
SG_NO_RULE_FQ_NAME = ['default-domain', 'default-project', '__no_rule__']
DCI_VN_FQ_NAME = ['default-domain', 'default-project', 'dci-network']
DCI_IPAM_FQ_NAME = ['default-domain', 'default-project', 'default-dci-lo0-network-ipam']

BGP_RTGT_MIN_ID = 8000000
SGID_MIN_ALLOC = 8000000
VNID_MIN_ALLOC = 1

PERMS_NONE = 0
PERMS_X = 1
PERMS_W = 2
PERMS_R = 4
PERMS_WX = 3
PERMS_RX = 5
PERMS_RW = 6
PERMS_RWX = 7

AAA_MODE_DEFAULT_VALUE = 'cloud-admin'
CLOUD_ADMIN_ROLE = 'admin'
GLOBAL_READ_ONLY_ROLE = None
PERMS2_VALID_SHARE_TYPES = ['tenant', 'domain']
DOMAIN_SHARING_PERMS = PERMS_RW

proto_dict = {
    'any': 0,
    'icmp': 1,
    'tcp': 6,
    'udp': 17,
}


RULE_IMPLICIT_ALLOW_UUID = "00000000-0000-0000-0000-100000000001"
RULE_IMPLICIT_DENY_UUID = "00000000-0000-0000-0000-100000000002"


def obj_to_json(obj):
    return dict((k, v) for k, v in obj.__dict__.iteritems())
# end obj_to_json


def json_to_obj(obj):
    pass
# end json_to_obj


def ignore_exceptions(func):
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except Exception as e:
            return None
    return wrapper
# end ignore_exceptions


# License: BSD-3
# Copyright (C) 2003-2018 Edgewall Software
# https://trac-hacks.org/changeset/13729/xmlrpcplugin/trunk/tracrpc/xml_rpc.py
_illegal_unichrs = [(0x00, 0x08), (0x0B, 0x0C), (0x0E, 0x1F),
                    (0x7F, 0x84), (0x86, 0x9F),
                    (0xFDD0, 0xFDDF), (0xFFFE, 0xFFFF)]
if sys.maxunicode >= 0x10000:  # not narrow build
    _illegal_unichrs.extend([(0x1FFFE, 0x1FFFF), (0x2FFFE, 0x2FFFF),
                             (0x3FFFE, 0x3FFFF), (0x4FFFE, 0x4FFFF),
                             (0x5FFFE, 0x5FFFF), (0x6FFFE, 0x6FFFF),
                             (0x7FFFE, 0x7FFFF), (0x8FFFE, 0x8FFFF),
                             (0x9FFFE, 0x9FFFF), (0xAFFFE, 0xAFFFF),
                             (0xBFFFE, 0xBFFFF), (0xCFFFE, 0xCFFFF),
                             (0xDFFFE, 0xDFFFF), (0xEFFFE, 0xEFFFF),
                             (0xFFFFE, 0xFFFFF), (0x10FFFE, 0x10FFFF)])

_illegal_ranges = ["%s-%s" % (unichr(low), unichr(high))
                   for (low, high) in _illegal_unichrs]
illegal_xml_chars_RE = re.compile(u'[%s]' % u''.join(_illegal_ranges))

HEX_ELEM = '[0-9A-Fa-f]'
UUID_PATTERN = '-'.join([HEX_ELEM + '{8}', HEX_ELEM + '{4}',
                         HEX_ELEM + '{4}', HEX_ELEM + '{4}',
                         HEX_ELEM + '{12}'])
UUID_WIHTOUT_DASH_PATTERN = ''.join([HEX_ELEM + '{8}', HEX_ELEM + '{4}',
                                     HEX_ELEM + '{4}', HEX_ELEM + '{4}',
                                     HEX_ELEM + '{12}'])


def _format_uuid_string(string):
    return (string.replace('urn:', '')
                  .replace('uuid:', '')
                  .strip('{}')
                  .replace('-', '')
                  .lower())


def is_uuid_like(val):
    """Returns validation of a value as a UUID.

    :param val: Value to verify
    :type val: string
    :returns: bool
    """
    try:
        return str(uuid.UUID(val)).replace('-', '') == _format_uuid_string(val)
    except (TypeError, ValueError, AttributeError):
        return False


def has_role(role, roles):
    """ Check if the a role is contained in a role list

    Looks if a role is contained to a list independently to the case
    sensitivity.
    """
    if role is None or roles is None:
        return False
    return role.lower() in [r.lower() for r in roles]


def get_lr_internal_vn_name(uuid):
    return '__contrail_lr_internal_vn_' + uuid + '__'

def get_dci_internal_vn_name(uuid):
    return '__contrail_dci_internal_vn_' + uuid + '__'


def _obj_serializer_all(obj):
    if hasattr(obj, 'serialize_to_json'):
        return obj.serialize_to_json()
    else:
        return dict((k, v) for k, v in obj.__dict__.iteritems())
