#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from bitarray import bitarray
from netaddr import *
import json
import cfgm_common.exceptions


class AddrMgmtError(Exception):
    pass
# end class AddrMgmtError


class AddrMgmtSubnetUndefined(AddrMgmtError):

    def __init__(self, vn_fq_name):
        self.vn_fq_name = vn_fq_name
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has no defined subnet(s)" %\
            (self.vn_fq_name)
    # end __str__
# end AddrMgmtSubnetUndefined


class AddrMgmtSubnetExhausted(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_val):
        self.vn_fq_name = vn_fq_name
        self.subnet_val = subnet_val
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has exhausted subnet(%s)" %\
            (self.vn_fq_name, self.subnet_val)
    # end __str__
# end AddrMgmtSubnetExhausted

# Class to manage a single subnet
#  maintain free list of IP addresses, exclude list and CIDR info


class Subnet(object):

    """Create a subnet with prefix and len

    Gateway (if provided) is made unavailable for assignment.
    Inuse mask represent addresses already assigned and in use during previous
    incarnations of api server. These are also taken out of free pool to
    prevent duplicate assignment.
    """
    _db_conn = None

    @classmethod
    def set_db_conn(cls, db_conn):
        cls._db_conn = db_conn
    # end set_db_conn

    def __init__(self, name, prefix, prefix_len, gw=None):
        self._version = 0

        """
        print 'Name = %s, prefix = %s, len = %s, gw = %s, db_conn = %s' \
            % (name, prefix, prefix_len, gw, 'Yes' if db_conn else 'No')
        """

        network = IPNetwork('%s/%s' % (prefix, prefix_len))

        # Exclude host, broadcast and gateway addresses
        exclude = [IPAddress(network.first), IPAddress(
            network.last), network.broadcast]
        if gw:
            gw_ip = IPAddress(gw)
            exclude.append(gw_ip)
        else:
            # reserve a gateway ip in subnet
            gw_ip = IPAddress(network.last - 1)
            exclude.append(gw_ip)

        self._db_conn.subnet_create_allocator(name, network.first, network.last)

        # reserve excluded addresses
        for addr in exclude:
            self._db_conn.subnet_alloc_req(name, int(addr))

        self._name = name
        self._network = network
        self._exclude = exclude
        self.gw_ip = gw_ip
    # end __init__

    @classmethod
    def delete_cls(cls, subnet_name):
        # deletes the index allocator
        cls._db_conn.subnet_delete_allocator(subnet_name)
    # end delete_cls

    def get_name(self):
        return self._name
    #end get_name

    def get_exclude(self):
        return self._exclude
    # end get_exclude

    def ip_alloc(self, ipaddr=None):
        req = None
        if ipaddr:
            ip = IPAddress(ipaddr)
            req = int(ip)
    
        addr = self._db_conn.subnet_alloc_req(self._name, req)
        if addr:
            return str(IPAddress(addr))
        return None
    # end ip_alloc

    # free IP unless it is invalid, excluded or already freed
    @classmethod
    def ip_free_cls(cls, subnet_fq_name, ip_network, exclude_addrs, ip_addr):
        if ((ip_addr in ip_network) and (ip_addr not in exclude_addrs)):
            if cls._db_conn:
                cls._db_conn.subnet_free_req(subnet_fq_name, int(ip_addr))
                return True

        return False
    # end ip_free_cls

    def ip_free(self, ip_addr):
        Subnet.ip_free_cls(self._name, self._network, self._exclude, ip_addr)
    # end ip_free

    # check if IP address belongs to us
    @classmethod
    def ip_belongs_to(cls, ipnet, ipaddr):
        return IPAddress(ipaddr) in ipnet
    # end ip_belongs_to

    def ip_belongs(self, ipaddr):
        return self.ip_belongs_to(self._network, ipaddr)
    # end ip_belongs

    def set_version(self, version):
        self._version = version
    # end set_version

    def get_version(self):
        return self._version
    # end get_version

# end class Subnet


# Address management for virtual network
class AddrMgmt(object):

    def __init__(self, server_mgr):
        #self.vninfo = {}
        self.version = 0
        self._server_mgr = server_mgr
        self._db_conn = None
        # dict of VN where each key has dict of subnets
        self._subnet_objs = {}
    # end __init__

    def _get_db_conn(self):
        if not self._db_conn:
            self._db_conn = self._server_mgr.get_db_connection()
            Subnet.set_db_conn(self._db_conn)

        return self._db_conn
    # end _get_db_conn

    def _get_subnet_dicts(self, vn_fq_name, vn_dict=None):
        db_conn = self._get_db_conn()
        vn_uuid = db_conn.fq_name_to_uuid('virtual-network', vn_fq_name)

        # Read in the VN details if not passed in
        if not vn_dict:
            (ok, result) = self._db_conn.dbe_read(
                                obj_type='virtual-network',
                                obj_ids={'uuid': vn_uuid},
                                obj_fields=['network_ipam_refs'])
            if not ok:
                raise VncError(result)

            vn_dict = result

        vn_fq_name_str = ':'.join(vn_fq_name)
        ipam_refs = vn_dict.get('network_ipam_refs', [])

        # gather all subnets, return dict keyed by name
        subnet_dicts = {}
        for ipam_ref in ipam_refs:
            vnsn_data = ipam_ref['attr']
            ipam_subnets = vnsn_data['ipam_subnets']
            for ipam_subnet in ipam_subnets:
                subnet_dict = ipam_subnet['subnet']
                subnet_dict['gw'] = ipam_subnet['default_gateway']
                subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                              subnet_dict['ip_prefix_len'])
                subnet_dicts[subnet_name] = subnet_dict

        return subnet_dicts
    # end _get_subnet_dicts

    def _create_subnet_objs(self, vn_fq_name_str, vn_dict):
        self._subnet_objs[vn_fq_name_str] = {}
        # create subnet for each new subnet
        refs = vn_dict.get('network_ipam_refs', None)
        if refs:
            for ref in refs:
                ipam_fq_name_str = ':'.join(ref['to'])
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])

                    gateway_ip = ipam_subnet.get('default_gateway', None)

                    subnet_obj = Subnet(
                        '%s:%s' % (vn_fq_name_str, subnet_name),
                        subnet['ip_prefix'], str(subnet['ip_prefix_len']),
                        gw=gateway_ip)
                    self._subnet_objs[vn_fq_name_str][subnet_name] = \
                         subnet_obj
                    ipam_subnet['default_gateway'] = str(subnet_obj.gw_ip)
    # end _create_subnet_objs

    def net_create_req(self, obj_dict):
        vn_fq_name_str = ':'.join(obj_dict['fq_name'])

        self._create_subnet_objs(vn_fq_name_str, obj_dict)
    # end net_create_req

    def net_create_notify(self, obj_ids, obj_dict):
        db_conn = self._get_db_conn()
        try:
            (ok, result) = db_conn.dbe_read(
                               'virtual-network',
                               obj_ids={'uuid': obj_ids['uuid']},
                               obj_fields=['fq_name', 'network_ipam_refs'])
        except cfgm_common.exceptions.NoIdError:
            return

        if not ok:
            print "Error: %s in net_create_notify" %(result)
            return

        vn_dict = result
        vn_fq_name_str = ':'.join(vn_dict['fq_name'])
        self._create_subnet_objs(vn_fq_name_str, vn_dict)
    # end net_create_notify

    def net_update_req(self, db_vn_dict, req_vn_dict, obj_uuid=None):
        # ideally 3 way sync/audit needed here. DB to what we is in subnet_objs
        # DB to what is in request. To simplify blow away subnet_objs and do
        # sync only from DB to request.
        vn_fq_name = db_vn_dict['fq_name']
        vn_fq_name_str = ':'.join(vn_fq_name)

        try:
            del self._subnet_objs[vn_fq_name_str]
        except KeyError:
            pass

        db_subnet_dicts = self._get_subnet_dicts(vn_fq_name, db_vn_dict)
        req_subnet_dicts = self._get_subnet_dicts(vn_fq_name, req_vn_dict)

        db_subnet_names = set([sname for sname in db_subnet_dicts])
        req_subnet_names = set([sname for sname in req_subnet_dicts])

        del_subnet_names = db_subnet_names - req_subnet_names
        add_subnet_names = req_subnet_names - db_subnet_names

        for subnet_name in del_subnet_names:
            Subnet.delete_cls(subnet_name)

        self._create_subnet_objs(vn_fq_name_str, req_vn_dict)
    # end net_update_req

    def net_update_notify(self, obj_ids):
        db_conn = self._get_db_conn()
        try:
            (ok, result) = db_conn.dbe_read(
                                obj_type='virtual-network',
                                obj_ids={'uuid': obj_ids['uuid']},
                                obj_fields=['fq_name', 'network_ipam_refs'])
        except cfgm_common.exceptions.NoIdError:
            return

        if not ok:
            print "Error: %s in net_update_notify" %(result)
            return

        vn_dict = result
        vn_fq_name_str = ':'.join(vn_dict['fq_name'])
        try:
            del self._subnet_objs[vn_fq_name_str]
        except KeyError:
            pass

        self._create_subnet_objs(vn_fq_name_str, vn_dict)
    # end net_update_notify

    # purge all subnets associated with a virtual network
    def net_delete_req(self, obj_dict):
        vn_fq_name = obj_dict['fq_name']
        subnet_dicts = self._get_subnet_dicts(vn_fq_name)

        for subnet_name in subnet_dicts:
            Subnet.delete_cls(subnet_name)

        try:
            vn_fq_name_str = ':'.join(vn_fq_name)
            del self._subnet_objs[vn_fq_name_str]
        except KeyError:
            pass
    # end net_delete_req

    def net_delete_notify(self, obj_ids, obj_dict):
        try:
            vn_fq_name_str = ':'.join(obj_dict['fq_name'])
            del self._subnet_objs[vn_fq_name_str]
        except KeyError:
            pass
    # end net_delete_notify

    def _vn_to_subnets(self, obj_dict):
        # given a VN return its subnets in list of net/prefixlen strings
