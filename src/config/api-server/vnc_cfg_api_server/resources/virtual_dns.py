#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#


from builtins import str
import re
import socket

from cfgm_common import PERMS_RX
from vnc_api.gen.resource_common import VirtualDns

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualDnsServer(ResourceMixin, VirtualDns):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for virtual DNS
        domain_uuid = obj_dict.get('parent_uuid')
        if domain_uuid is None:
            domain_uuid = db_conn.fq_name_to_uuid('domain',
                                                  obj_dict['fq_name'][0:1])
        share_item = {
            'tenant': 'domain:%s' % domain_uuid,
            'tenant_access': PERMS_RX
        }
        obj_dict['perms2'].setdefault('share', []).append(share_item)
        return cls.validate_dns_server(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_dns_server(obj_dict, db_conn)

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        vdns_name = ":".join(obj_dict['fq_name'])
        if 'parent_uuid' in obj_dict:
            ok, read_result = cls.dbe_read(db_conn, 'domain',
                                           obj_dict['parent_uuid'])
            if not ok:
                return ok, read_result, None
            virtual_DNSs = read_result.get('virtual_DNSs') or []
            for vdns in virtual_DNSs:
                ok, read_result = cls.dbe_read(db_conn, 'virtual_DNS',
                                               vdns['uuid'])
                if not ok:
                    code, msg = read_result
                    if code == 404:
                        continue
                    return ok, (code, msg), None

                vdns_data = read_result['virtual_DNS_data']
                if 'next_virtual_DNS' in vdns_data:
                    if vdns_data['next_virtual_DNS'] == vdns_name:
                        return (
                            False,
                            (403,
                             "Virtual DNS server is referred"
                             " by other virtual DNS servers"), None)
        return True, "", None

    @classmethod
    def is_valid_dns_name(cls, name):
        if len(name) > 255:
            return False
        if name.endswith("."):  # A single trailing dot is legal
            # strip exactly one dot from the right, if present
            name = name[:-1]
        disallowed = re.compile(r'[^A-Z\d-]', re.IGNORECASE)
        return all(  # Split by labels and verify individually
            (label and len(label) <= 63 and  # length is within proper range
             # no bordering hyphens
             not label.startswith("-") and not label.endswith("-") and
             not disallowed.search(label))  # contains only legal char
            for label in name.split("."))

    @classmethod
    def is_valid_ipv4_address(cls, address):
        parts = address.split(".")
        if len(parts) != 4:
            return False
        for item in parts:
            try:
                if not 0 <= int(item) <= 255:
                    return False
            except ValueError:
                return False
        return True

    @classmethod
    def is_valid_ipv6_address(cls, address):
        try:
            socket.inet_pton(socket.AF_INET6, address)
        except socket.error:
            return False
        return True

    @classmethod
    def validate_dns_server(cls, obj_dict, db_conn):
        if 'fq_name' in obj_dict:
            virtual_dns = obj_dict['fq_name'][1]
            disallowed = re.compile(r'[^A-Z\d-]', re.IGNORECASE)
            if disallowed.search(virtual_dns) or virtual_dns.startswith("-"):
                msg = ("Special characters are not allowed in Virtual DNS "
                       "server name")
                return False, (403, msg)

        vdns_data = obj_dict['virtual_DNS_data']
        if not cls.is_valid_dns_name(vdns_data['domain_name']):
            return (
                False,
                (403, "Domain name does not adhere to DNS name requirements"))

        record_order = ["fixed", "random", "round-robin"]
        if not str(vdns_data['record_order']).lower() in record_order:
            return (False, (403, "Invalid value for record order"))

        ttl = vdns_data['default_ttl_seconds']
        if ttl < 0 or ttl > 2147483647:
            return (False, (400, "Invalid value for TTL"))

        if 'next_virtual_DNS' in vdns_data:
            vdns_next = vdns_data['next_virtual_DNS']
            if not vdns_next or vdns_next is None:
                return True, ""
            next_vdns = vdns_data['next_virtual_DNS'].split(":")
            # check that next vdns exists
            try:
                next_vdns_uuid = db_conn.fq_name_to_uuid(
                    'virtual_DNS', next_vdns)
            except Exception:
                if not cls.is_valid_ipv4_address(
                        vdns_data['next_virtual_DNS']):
                    return (
                        False,
                        (400,
                         "Invalid Virtual Forwarder(next virtual dns server)"))
                else:
                    return True, ""
            # check that next virtual dns servers arent referring to each other
            # above check doesnt allow during create, but entry could be
            # modified later
            ok, read_result = cls.dbe_read(db_conn, 'virtual_DNS',
                                           next_vdns_uuid)
            if ok:
                next_vdns_data = read_result['virtual_DNS_data']
                if 'next_virtual_DNS' in next_vdns_data:
                    vdns_name = ":".join(obj_dict['fq_name'])
                    if next_vdns_data['next_virtual_DNS'] == vdns_name:
                        return (
                            False,
                            (403,
                             "Cannot have Virtual DNS Servers "
                             "referring to each other"))
        return True, ""
