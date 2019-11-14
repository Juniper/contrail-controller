#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#


from builtins import str

from vnc_api.gen.resource_common import VirtualDnsRecord

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualDnsRecordServer(ResourceMixin, VirtualDnsRecord):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_dns_record(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_dns_record(obj_dict, db_conn)

    @classmethod
    def validate_dns_record(cls, obj_dict, db_conn):
        vdns_class = cls.server.get_resource_class('virtual_DNS')
        rec_data = obj_dict['virtual_DNS_record_data']
        rec_types = ["a", "cname", "ptr", "ns", "mx", "aaaa"]
        rec_type = str(rec_data['record_type']).lower()
        if rec_type not in rec_types:
            return (False, (403, "Invalid record type"))
        if str(rec_data['record_class']).lower() != "in":
            return (False, (403, "Invalid record class"))

        rec_name = rec_data['record_name']
        rec_value = rec_data['record_data']

        # check rec_name validity
        if rec_type == "ptr":
            if (not vdns_class.is_valid_ipv4_address(rec_name) and
                    not vdns_class.is_valid_ipv6_address(rec_name) and
                    "in-addr.arpa" not in rec_name.lower() and
                    "ip6.arpa" not in rec_name.lower()):
                msg = ("PTR Record name has to be IP address or "
                       "reverse.ip.in-addr.arpa")
                return False, (403, msg)
        elif not vdns_class.is_valid_dns_name(rec_name):
            msg = "Record name does not adhere to DNS name requirements"
            return False, (403, msg)

        # check rec_data validity
        if rec_type == "a":
            if not vdns_class.is_valid_ipv4_address(rec_value):
                return (False, (403, "Invalid IP address"))
        elif rec_type == "aaaa":
            if not vdns_class.is_valid_ipv6_address(rec_value):
                return (False, (403, "Invalid IPv6 address"))
        elif rec_type == "cname" or rec_type == "ptr" or rec_type == "mx":
            if not vdns_class.is_valid_dns_name(rec_value):
                return (
                    False,
                    (403,
                     "Record data does not adhere to DNS name requirements"))
        elif rec_type == "ns":
            try:
                vdns_name = rec_value.split(":")
                db_conn.fq_name_to_uuid('virtual_DNS', vdns_name)
            except Exception:
                if (not vdns_class.is_valid_ipv4_address(rec_value) and
                        not vdns_class.is_valid_dns_name(rec_value)):
                    msg = "Invalid virtual dns server in record data"
                    return False, (403, msg)

        ttl = rec_data['record_ttl_seconds']
        if ttl < 0 or ttl > 2147483647:
            return (False, (403, "Invalid value for TTL"))

        if rec_type == "mx":
            preference = rec_data['record_mx_preference']
            if preference < 0 or preference > 65535:
                return (False, (403, "Invalid value for MX record preference"))

        return True, ""
