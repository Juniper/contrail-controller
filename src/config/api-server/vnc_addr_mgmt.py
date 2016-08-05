#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import copy
import uuid
from netaddr import IPNetwork, IPAddress, IPRange, all_matching_cidrs
from vnc_quota import QuotaHelper
from pprint import pformat
import cfgm_common.exceptions
import cfgm_common.utils
try:
    # python2.7
    from collections import OrderedDict
except:
    # python2.6
    from ordereddict import OrderedDict

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


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


class AddrMgmtAllocUnitInvalid(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_name, alloc_unit):
        self.vn_fq_name = vn_fq_name
        self.subnet_name = subnet_name
        self.alloc_unit = alloc_unit
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has invalid alloc_unit(%s) in subnet(%s)" %\
            (self.vn_fq_name, self.alloc_unit, self.subnet_name)
    # end __str__
# end AddrMgmtAllocUnitInvalid

class AddrMgmtSubnetInvalid(AddrMgmtError):

    def __init__(self, vn_fq_name, subnet_name):
        self.vn_fq_name = vn_fq_name
        self.subnet_name = subnet_name
    # end __init__

    def __str__(self):
        return "Virtual-Network(%s) has invalid subnet(%s)" %\
            (self.vn_fq_name, self.subnet_name)
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


class AddrMgmtInvalidIpAddr(AddrMgmtError):

    def __init__(self, subnet_val, alloc_pool):
        self.subnet_val = subnet_val
        self.alloc_pool = alloc_pool
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Ip address in Allocation Pool(%s)" %\
            (self.subnet_val, self.alloc_pool)
    # end __str__
# end AddrMgmtInvalidIpAddr


class AddrMgmtOutofBoundAllocPool(AddrMgmtError):

    def __init__(self, subnet_val, alloc_pool):
        self.subnet_val = subnet_val
        self.alloc_pool = alloc_pool
    # end __init__

    def __str__(self):
        return "subnet(%s) allocation pool (%s) is out of cidr" %\
            (self.subnet_val, self.alloc_pool)
    # end __str__
# end AddrMgmtOutofBoundAllocPool


class AddrMgmtInvalidAllocPool(AddrMgmtError):

    def __init__(self, subnet_val, alloc_pool):
        self.subnet_val = subnet_val
        self.alloc_pool = alloc_pool
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Allocation Pool(%s)" %\
            (self.subnet_val, self.alloc_pool)
    # end __str__
# end AddrMgmtInvalidAllocPool


class AddrMgmtInvalidGatewayIp(AddrMgmtError):

    def __init__(self, subnet_val, gateway_ip):
        self.subnet_val = subnet_val
        self.gw_ip = gateway_ip
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Gateway ip address(%s)" %\
            (self.subnet_val, self.gw_ip)
    # end __str__
# end AddrMgmtInvalidGatewayIp


class AddrMgmtInvalidServiceNodeIp(AddrMgmtError):

    def __init__(self, subnet_val, service_address):
        self.subnet_val = subnet_val
        self.service_address = service_address
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid Service Node ip address(%s)" %\
            (self.subnet_val, self.service_address)
    # end __str__
# end AddrMgmtInvalidServiceNodeIp


class AddrMgmtInvalidDnsNameServer(AddrMgmtError):

    def __init__(self, subnet_val, name_server):
        self.subnet_val = subnet_val
        self.gw_ip = name_server
    # end __init__

    def __str__(self):
        return "subnet(%s) has Invalid DNS Nameserver(%s)" %\
            (self.subnet_val, self.name_server)
    # end __str__
# end AddrMgmtInvalidGatewayIp


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

    def __init__(self, name, prefix, prefix_len,
                 gw=None, service_address=None, enable_dhcp=True,
                 dns_nameservers=None,
                 alloc_pool_list=None,
                 addr_from_start=False,
                 should_persist=True,
                 ip_alloc_unit=1):

        network = IPNetwork('%s/%s' % (prefix, prefix_len))

        # check allocation-pool
        for ip_pool in alloc_pool_list or []:
            try:
                start_ip = IPAddress(ip_pool['start'])
                end_ip = IPAddress(ip_pool['end'])
            except AddrFormatError:
                raise AddrMgmtInvalidIpAddr(name, ip_pool)
            if (start_ip not in network or end_ip not in network):
                raise AddrMgmtOutofBoundAllocPool(name, ip_pool)
            if (end_ip < start_ip):
                raise AddrMgmtInvalidAllocPool(name, ip_pool)

        # check gw
        if gw:
            try:
                gw_ip = IPAddress(gw)
            except AddrFormatError:
                raise AddrMgmtInvalidGatewayIp(name, gw_ip)

        else:
            # reserve a gateway ip in subnet
            if addr_from_start:
                gw_ip = IPAddress(network.first + 1)
            else:
                gw_ip = IPAddress(network.last - 1)

        # check service_address
        if service_address:
            try:
                service_node_address = IPAddress(service_address)
            except AddrFormatError:
                raise AddrMgmtInvalidServiceNodeIp(name, service_node_address)

        else:
            # reserve a service address ip in subnet
            if addr_from_start:
                service_node_address = IPAddress(network.first + 2)
            else:
                service_node_address = IPAddress(network.last - 2)

        # check dns_nameservers
        for nameserver in dns_nameservers or []:
            try:
                ip_addr = IPAddress(nameserver)
            except AddrFormatError:
                raise AddrMgmtInvalidDnsServer(name, nameserver)

        # check allocation-unit
        # alloc-unit should be power of 2
        if (ip_alloc_unit & (ip_alloc_unit-1)):
            raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                                           ip_alloc_unit)

        # if allocation-pool is not specified, create one with entire cidr
        no_alloc_pool = False
        if not alloc_pool_list:
            alloc_pool_list = [{'start': str(IPAddress(network.first)),
                                'end': str(IPAddress(network.last-1))}]
            no_alloc_pool = True


        # need alloc_pool_list with integer to use in Allocator
        alloc_int_list = list()
        # store integer for given ip address in allocation list
        for alloc_pool in alloc_pool_list:
            alloc_int = {'start': int(IPAddress(alloc_pool['start'])),
                         'end': int(IPAddress(alloc_pool['end']))}
            alloc_int_list.append(alloc_int)

        # check alloc_pool starts at integer multiple of alloc_unit
        if ip_alloc_unit is not 1:
            for alloc_int in alloc_int_list:
                if alloc_int['start'] % ip_alloc_unit:
                    raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                                                   ip_alloc_unit)

        # go through each alloc_pool and validate allocation pool
        # given ip_alloc_unit
        net_ip = IPAddress(network.first)
        bc_ip = IPAddress(network.last)
        for alloc_pool in alloc_pool_list:
            if no_alloc_pool:
                start_ip = IPAddress(network.first)
                end_ip = IPAddress(network.last)
            else:
                start_ip = IPAddress(alloc_pool['start'])
                end_ip = IPAddress(alloc_pool['end'])

            alloc_pool_range = int(end_ip) - int(start_ip) + 1
            # each alloc-pool range should be integer multiple of ip_alloc_unit
            if (alloc_pool_range < ip_alloc_unit):
                raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                                               ip_alloc_unit)
            if (alloc_pool_range % ip_alloc_unit):
                raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                                               ip_alloc_unit)

            block_alloc_unit = 0
            if (net_ip == start_ip):
                block_alloc_unit += 1
                if (bc_ip == end_ip):
                    if (int(end_ip) - int(start_ip) >= ip_alloc_unit):
                        block_alloc_unit += 1
            else:
                if (bc_ip == end_ip):
                    block_alloc_unit += 1

            if (gw_ip >= start_ip and gw_ip <= end_ip):
                #gw_ip is part of this alloc_pool, block another alloc_unit
                # only if gw_ip is not a part of already counted block units.
                if ((int(gw_ip) - int(start_ip) >= ip_alloc_unit) and 
                    (int(end_ip) - int(gw_ip) >= ip_alloc_unit)):
                    block_alloc_unit += 1

            if (service_node_address >=start_ip and
                service_node_address <= end_ip):
                #service_node_address is part of this alloc_pool,
                #block another alloc_unit only if service_node_address is not
                #part of already counted block units.
                if ((int(service_node_address) -
                     int(start_ip) >= ip_alloc_unit) and
                    (int(end_ip) -
                     int(service_node_address) >= ip_alloc_unit) and
                    (abs(int(gw_ip) -
                         int(service_node_address)) > ip_alloc_unit)):
                    block_alloc_unit += 1

            # each alloc-pool should have minimum block_alloc_unit+1,
            # possible allocation
            if (alloc_pool_range/ip_alloc_unit) <= block_alloc_unit:
                raise AddrMgmtAllocUnitInvalid(name, prefix+'/'+prefix_len,
                                               ip_alloc_unit)

        # Exclude host and broadcast
        exclude = [IPAddress(network.first), network.broadcast]

        #check size of subnet or individual alloc_pools are multiple of alloc_unit

        # exclude gw_ip, service_node_address if they are within
        # allocation-pool
        for alloc_int in alloc_int_list:
            if alloc_int['start'] <= int(gw_ip) <= alloc_int['end']:
                exclude.append(gw_ip)
            if (alloc_int['start'] <= int(service_node_address)
                    <= alloc_int['end']):
                exclude.append(service_node_address)
        # ip address allocator will be per alloc-unit
        self._db_conn.subnet_create_allocator(name, alloc_int_list,
                                              addr_from_start,
                                              should_persist,
                                              network.first,
                                              network.size,
                                              ip_alloc_unit)

        # reserve excluded addresses
        for addr in exclude:
            if should_persist:
                self._db_conn.subnet_reserve_req(name, int(addr)/ip_alloc_unit,
                                                 'system-reserved')
            else:
                self._db_conn.subnet_set_in_use(name, int(addr))

        self._name = name
        self._network = network
        self._version = network.version
        self._exclude = exclude
        self.gw_ip = gw_ip
        self.dns_server_address = service_node_address
        self._alloc_pool_list = alloc_pool_list
        self.enable_dhcp = enable_dhcp
        self.dns_nameservers = dns_nameservers
        self.alloc_unit = ip_alloc_unit
        self._prefix = prefix
        self._prefix_len = prefix_len
    # end __init__

    @classmethod
    def delete_cls(cls, subnet_name):
        # deletes the index allocator
        cls._db_conn.subnet_delete_allocator(subnet_name)
    # end delete_cls

    def get_name(self):
        return self._name
    # end get_name

    def get_exclude(self):
        return self._exclude
    # end get_exclude

    # ip address management helper routines.
    # ip_set/reset_in_use do not persist to DB just internal bitmap maintenance
    # (for e.g. for notify context)
    # ip_reserve/alloc/free persist to DB
    def is_ip_allocated(self, ipaddr):
        ip = IPAddress(ipaddr)
        addr = int(ip)
        return self._db_conn.subnet_is_addr_allocated(self._name, addr)
    # end is_ip_allocated

    def ip_set_in_use(self, ipaddr):
        ip = IPAddress(ipaddr)
        addr = int(ip)
        return self._db_conn.subnet_set_in_use(self._name, addr)
    # end ip_set_in_use

    def ip_reset_in_use(self, ipaddr):
        ip = IPAddress(ipaddr)
        addr = int(ip)
        return self._db_conn.subnet_reset_in_use(self._name, addr)
    # end ip_reset_in_use

    def ip_reserve(self, ipaddr, value):
        ip = IPAddress(ipaddr)
        req = int(ip)

        addr = self._db_conn.subnet_reserve_req(self._name, req, value)
        if addr:
            return str(IPAddress(addr))
        return None
    # end ip_reserve

    def ip_count(self):
        return self._db_conn.subnet_alloc_count(self._name)
    # end ip_count

    def ip_alloc(self, ipaddr=None, value=None):
        if ipaddr:
            #check if ipaddr is multiple of alloc_unit
            if ipaddr % self.alloc_unit:
                raise AddrMgmtAllocUnitInvalid(self._name,
                          self._prefix+'/'+self._prefix_len, self.alloc_unit)
            return self.ip_reserve(ipaddr/self.alloc_unit, value)

        addr = self._db_conn.subnet_alloc_req(self._name, value)
        if addr:
            return str(IPAddress(addr * self.alloc_unit))
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
        # self.vninfo = {}
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

        # Read in the VN details if not passed in
        if not vn_dict:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
            (ok, result) = self._db_conn.dbe_read(
                                obj_type='virtual_network',
                                obj_ids={'uuid': vn_uuid},
                                obj_fields=['network_ipam_refs'])

            if not ok:
                raise VncError(result)

            vn_dict = result

        vn_fq_name_str = ':'.join(vn_fq_name)
        ipam_refs = vn_dict.get('network_ipam_refs', [])

        # gather all subnets, return dict keyed by name
        subnet_dicts = OrderedDict()
        for ipam_ref in ipam_refs:
            vnsn_data = ipam_ref['attr']
            ipam_subnets = vnsn_data['ipam_subnets']
            for ipam_subnet in ipam_subnets:
                subnet_dict = copy.deepcopy(ipam_subnet['subnet'])
                subnet_dict['gw'] = ipam_subnet.get('default_gateway', None)
                subnet_dict['alloc_unit'] = ipam_subnet.get('alloc_unit', 1)
                subnet_dict['dns_server_address'] = ipam_subnet.get('dns_server_address', None)
                subnet_dict['allocation_pools'] = \
                    ipam_subnet.get('allocation_pools', None)
                subnet_dict['enable_dhcp'] = ipam_subnet.get('enable_dhcp', True)
                subnet_dict['dns_nameservers'] = ipam_subnet.get('dns_nameservers', None)
                subnet_dict['addr_start'] = ipam_subnet.get('addr_from_start',
                                                            False)
                subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                              subnet_dict['ip_prefix_len'])
                subnet_dicts[subnet_name] = subnet_dict

        return subnet_dicts
    # end _get_subnet_dicts

    def _create_subnet_objs(self, vn_fq_name_str, vn_dict, should_persist):
        self._subnet_objs.setdefault(vn_fq_name_str, {})
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

                    try:
                        subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
                    except KeyError:
                        gateway_ip = ipam_subnet.get('default_gateway')
                        service_address = ipam_subnet.get('dns_server_address')
                        allocation_pools = ipam_subnet.get('allocation_pools')
                        dhcp_config = ipam_subnet.get('enable_dhcp', True)
                        nameservers = ipam_subnet.get('dns_nameservers')
                        addr_start = ipam_subnet.get('addr_from_start', False)
                        alloc_unit = ipam_subnet.get('alloc_unit', 1)
                        subnet_obj = Subnet(
                            '%s:%s' % (vn_fq_name_str, subnet_name),
                            subnet['ip_prefix'], str(subnet['ip_prefix_len']),
                            gw=gateway_ip, service_address=service_address,
                            enable_dhcp=dhcp_config,
                            dns_nameservers=nameservers,
                            alloc_pool_list=allocation_pools,
                            addr_from_start=addr_start,
                            should_persist=should_persist,
                            ip_alloc_unit=alloc_unit)
                        self._subnet_objs[vn_fq_name_str][subnet_name] = \
                             subnet_obj

                    ipam_subnet['default_gateway'] = str(subnet_obj.gw_ip)
                    ipam_subnet['dns_server_address'] = str(subnet_obj.dns_server_address)
    # end _create_subnet_objs

    def config_log(self, msg, level):
        self._server_mgr.config_log(msg, level)
    # end config_log

    def net_create_req(self, obj_dict):
        self._get_db_conn()
        vn_fq_name_str = ':'.join(obj_dict['fq_name'])

        self._create_subnet_objs(vn_fq_name_str, obj_dict, should_persist=True)
    # end net_create_req

    def net_create_notify(self, obj_ids, obj_dict):
        db_conn = self._get_db_conn()
        try:
            (ok, result) = db_conn.dbe_read(
                               'virtual_network',
                               obj_ids={'uuid': obj_ids['uuid']},
                               obj_fields=['fq_name', 'network_ipam_refs'])
        except cfgm_common.exceptions.NoIdError:
            return

        if not ok:
            db_conn.config_log("Error: %s in net_create_notify" %(result),
                               level=SandeshLevel.SYS_ERR)
            return

        vn_dict = result
        vn_fq_name_str = ':'.join(vn_dict['fq_name'])
        self._create_subnet_objs(vn_fq_name_str, vn_dict, should_persist=False)
    # end net_create_notify

    def net_update_req(self, vn_fq_name, db_vn_dict, req_vn_dict, obj_uuid=None):
        vn_fq_name_str = ':'.join(vn_fq_name)

        db_subnet_dicts = self._get_subnet_dicts(vn_fq_name, db_vn_dict)
        req_subnet_dicts = self._get_subnet_dicts(vn_fq_name, req_vn_dict)

        db_subnet_names = set(db_subnet_dicts.keys())
        req_subnet_names = set(req_subnet_dicts.keys())

        del_subnet_names = db_subnet_names - req_subnet_names
        add_subnet_names = req_subnet_names - db_subnet_names

        for subnet_name in del_subnet_names:
            Subnet.delete_cls('%s:%s' % (vn_fq_name_str, subnet_name))
            try:
                del self._subnet_objs[vn_fq_name_str][subnet_name]
            except KeyError:
                pass

        # check db_subnet_dicts and req_subnet_dicts
        # following parameters are same for subnets present in both dicts
        # 1. enable_dhcp, 2. default_gateway, 3. dns_server_address
        # 4. allocation_pool, 5. dns_nameservers
        for key in req_subnet_dicts.keys():
            req_subnet = req_subnet_dicts[key]
            if key in db_subnet_dicts.keys():
                db_subnet = db_subnet_dicts[key]
                if req_subnet['enable_dhcp'] is None:
                    req_subnet['enable_dhcp'] = True
                if (req_subnet['gw'] and req_subnet['gw'] != db_subnet['gw']):
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)
                req_subnet['dns_server_address'] = db_subnet['dns_server_address']

                req_alloc_list = req_subnet['allocation_pools'] or []
                db_alloc_list = db_subnet['allocation_pools'] or []
                if (len(req_alloc_list) != len(db_alloc_list)):
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)

                for index in range(len(req_alloc_list)):
                    if cmp(req_alloc_list[index], db_alloc_list[index]):
                        raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)

        self._create_subnet_objs(vn_fq_name_str, req_vn_dict, should_persist=True)
    # end net_update_req

    def net_update_notify(self, obj_ids):
        db_conn = self._get_db_conn()
        try:
            (ok, result) = db_conn.dbe_read(
                                obj_type='virtual_network',
                                obj_ids={'uuid': obj_ids['uuid']},
                                obj_fields=['fq_name', 'network_ipam_refs'])
        except cfgm_common.exceptions.NoIdError:
            return

        if not ok:
            db_conn.config_log("Error: %s in net_update_notify" %(result),
                               level=SandeshLevel.SYS_ERR)
            return

        vn_dict = result
        vn_fq_name_str = ':'.join(vn_dict['fq_name'])
        self._create_subnet_objs(vn_fq_name_str, vn_dict, should_persist=False)
    # end net_update_notify

    # purge all subnets associated with a virtual network
    def net_delete_req(self, obj_dict):
        vn_fq_name = obj_dict['fq_name']
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_dicts = self._get_subnet_dicts(vn_fq_name)

        for subnet_name in subnet_dicts:
            Subnet.delete_cls('%s:%s' % (vn_fq_name_str, subnet_name))

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

        ipam_refs = obj_dict.get('network_ipam_refs', None)
        if ipam_refs != None:
            subnet_list = []
            for ref in ipam_refs:
                vnsn_data = ref['attr']
                ipam_subnets = vnsn_data['ipam_subnets']
                for ipam_subnet in ipam_subnets:
                    subnet = ipam_subnet['subnet']
                    subnet_name = subnet['ip_prefix'] + '/' + str(
                        subnet['ip_prefix_len'])
                    subnet_list.append(subnet_name)
        else:
            subnet_list = None

        return subnet_list
    # end _vn_to_subnets

    def net_check_subnet_quota(self, db_vn_dict, req_vn_dict, db_conn):
        if 'parent_uuid' not in db_vn_dict:
            proj_fq_name = db_vn_dict['fq_name'][:-1]
            proj_uuid = db_conn.fq_name_to_uuid('project', proj_fq_name)
        else:
            proj_uuid = db_vn_dict['parent_uuid']

        (ok, proj_dict) = QuotaHelper.get_project_dict_for_quota(proj_uuid,
                                                                 db_conn)
        if not ok:
            return (False, 'Internal error : ' + pformat(proj_dict))

        obj_type = 'subnet'
        if QuotaHelper.get_quota_limit(proj_dict, obj_type) < 0:
            return True, ""
        subnets = self._vn_to_subnets(req_vn_dict)
        if not subnets:
            return True, ""
        # Read list of virtual networks for the given project
        (ok, result) = db_conn.dbe_list('virtual_network', [proj_uuid])
        if not ok:
            return (False, 'Internal error : Failed to read virtual networks')

        # Read network ipam refs for all virtual networks for the given project
        obj_ids_list = [{'uuid': obj_uuid} for _, obj_uuid in result if obj_uuid != db_vn_dict['uuid']]
        obj_fields = [u'network_ipam_refs']
        (ok, result) = db_conn.dbe_read_multi('virtual_network', obj_ids_list, obj_fields)
        if not ok:
            return (False, 'Internal error : Failed to read virtual networks')

        for net_dict in result:
            vn_subnets = self._vn_to_subnets(net_dict)
            if not vn_subnets:
                continue
            subnets.extend(vn_subnets)

        quota_count = len(subnets) - 1

        (ok, quota_limit) = QuotaHelper.check_quota_limit(proj_dict, obj_type,
                                                          quota_count)
        if not ok:
            return (False, pformat(db_vn_dict['fq_name']) + ' : ' + quota_limit)

        return True, ""

    # check subnets associated with a virtual network, return error if
    # any two subnets have overlap ip addresses
    def net_check_subnet_overlap(self, req_vn_dict):
        # get all subnets existing + requested and check any non-exact overlaps
        requested_subnets = self._vn_to_subnets(req_vn_dict)
        if not requested_subnets:
            return True, ""
        subnets = [IPNetwork(subnet) for subnet in requested_subnets]
        for i, net1 in enumerate(subnets):
            for net2 in subnets[i+1:]:
                if net1 in net2 or net2 in net1:
                    err_msg = "Overlapping addresses: "
                    return False, err_msg + str([net1, net2])

        return True, ""
    # end net_check_subnet_overlap

    def net_check_subnet(self, req_vn_dict):
        ipam_refs = req_vn_dict.get('network_ipam_refs', [])
        for ipam_ref in ipam_refs:
            vnsn_data = ipam_ref['attr']
            ipam_subnets = vnsn_data['ipam_subnets']
            for ipam_subnet in ipam_subnets:
                subnet_dict = copy.deepcopy(ipam_subnet['subnet'])
                prefix = subnet_dict['ip_prefix']
                prefix_len = subnet_dict['ip_prefix_len']
                network = IPNetwork('%s/%s' % (prefix, prefix_len))
                subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                    subnet_dict['ip_prefix_len'])

                # check subnet-uuid
                ipam_cfg_subnet_uuid = ipam_subnet.get('subnet_uuid', None)
                try:
                    if ipam_cfg_subnet_uuid:
                        subnet_uuid = uuid.UUID(ipam_cfg_subnet_uuid)
                except ValueError:
                    err_msg = "Invalid subnet-uuid %s in subnet:%s" \
                        %(ipam_cfg_subnet_uuid, subnet_name)
                    return False, err_msg

                # check allocation-pool
                alloc_pools = ipam_subnet.get('allocation_pools', None)
                for pool in alloc_pools or []:
                    try:
                        iprange = IPRange(pool['start'], pool['end'])
                    except AddrFormatError:
                        err_msg = "Invalid allocation Pool start:%s, end:%s in subnet:%s" \
                            %(pool['start'], pool['end'], subnet_name)
                        return False, err_msg
                    if iprange not in network:
                        err_msg = "allocation pool start:%s, end:%s out of cidr:%s" \
                            %(pool['start'], pool['end'], subnet_name)
                        return False, err_msg

                # check gw
                gw = ipam_subnet.get('default_gateway', None)
                if gw is not None:
                    try:
                        gw_ip = IPAddress(gw)
                    except AddrFormatError:
                        err_msg = "Invalid gateway Ip address:%s" \
                            %(gw)
                        return False, err_msg
                    if (gw_ip != IPAddress('0.0.0.0') and
                            gw_ip != IPAddress('::') and
                            (gw_ip < IPAddress(network.first + 1) or
                                gw_ip > IPAddress(network.last - 1))):
                        err_msg = "gateway Ip %s out of cidr: %s" \
                            %(gw, subnet_name)
                        return False, err_msg

                # check service address
                service_address = ipam_subnet.get('dns_server_address', None)
                if service_address is not None:
                    try:
                        service_node_address = IPAddress(service_address)
                    except AddrFormatError:
                        err_msg = "Invalid Dns Server Ip address:%s" \
                            %(service_address)
                        return False, err_msg
        return True, ""
    # end net_check_subnet

    # check subnets associated with a virtual network, return error if
    # any subnet is being deleted and has backref to
    # instance-ip/floating-ip/alias-ip
    def net_check_subnet_delete(self, db_vn_dict, req_vn_dict):
        db_conn = self._get_db_conn()
        # if all ips are part of requested list
        # things are ok.
        # eg. existing [1.1.1.0/24, 2.2.2.0/24],
        #     requested [1.1.1.0/24] OR
        #     requested [1.1.1.0/28, 2.2.2.0/24]
        existing_subnets = self._vn_to_subnets(db_vn_dict)
        if not existing_subnets:
            return True, ""
        requested_subnets = self._vn_to_subnets(req_vn_dict)
        if requested_subnets is None:
            # subnets not modified in request
            return True, ""

        delete_set = set(existing_subnets) - set(requested_subnets)

        if len(delete_set):
            # read the instance ip and floating ip pool only if subnet is being
            # deleted. Skip the port check if no subnet is being deleted
            vn_id = {'uuid': db_vn_dict['uuid']}
            obj_fields = ['network_ipam_refs', 'instance_ip_back_refs', 'floating_ip_pools', 'alias_ip_pools']
            (read_ok, db_vn_dict) = db_conn.dbe_read('virtual_network', vn_id, obj_fields)
            if not read_ok:
                return (False, (500, db_vn_dict))

            # if all subnets are being removed, check for any iip backrefs
            # or floating/alias pools still present in DB version of VN
            if len(requested_subnets) == 0:
                if db_vn_dict.get('instance_ip_back_refs'):
                    return False, "Cannot Delete IP Block, Instance IP(s) in use"
                if db_vn_dict.get('floating_ip_pools'):
                    return False, "Cannot Delete IP Block, Floating Pool(s) in use"
                if db_vn_dict.get('alias_ip_pools'):
                    return False, "Cannot Delete IP Block, Alias Pool(s) in use"
        else:
            return True, ""


        instip_refs = db_vn_dict.get('instance_ip_back_refs', [])
        for ref in instip_refs:
            try:
                (ok, result) = db_conn.dbe_read(
                    'instance_ip', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in subnet delete instance-ip check: %s" %(result),
                    level=SandeshLevel.SYS_ERR)
                return False, result

            inst_ip = result.get('instance_ip_address')
            if not inst_ip:
                self.config_log(
                    "Error in subnet delete ip null: %s" %(ref['uuid']),
                    level=SandeshLevel.SYS_ERR)
                continue
            if not all_matching_cidrs(inst_ip, requested_subnets):
                return (False,
                        "Cannot Delete IP Block, IP(%s) is in use"
                        % (inst_ip))

        fip_pool_refs = db_vn_dict.get('floating_ip_pools', [])
        for ref in fip_pool_refs:
            try:
                (ok, result) = db_conn.dbe_read(
                    'floating_ip_pool', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in subnet delete floating-ip-pool check: %s"
                        %(result),
                    level=SandeshLevel.SYS_ERR)
                return False, result

            floating_ips = result.get('floating_ips', [])
            for floating_ip in floating_ips:
                # get floating_ip_address and this should be in
                # new subnet_list
                try:
                    (read_ok, read_result) = db_conn.dbe_read(
                        'floating_ip', {'uuid': floating_ip['uuid']})
                except cfgm_common.exceptions.NoIdError:
                    continue
                if not read_ok:
                    self.config_log(
                        "Error in subnet delete floating-ip check: %s"
                            %(read_result),
                        level=SandeshLevel.SYS_ERR)
                    return False, result

                fip_addr = read_result.get('floating_ip_address')
                if not fip_addr:
                    self.config_log(
                        "Error in subnet delete fip null: %s"
                        %(floating_ip['uuid']),
                        level=SandeshLevel.SYS_ERR)
                    continue
                if not all_matching_cidrs(fip_addr, requested_subnets):
                    return (False,
                            "Cannot Delete IP Block, Floating IP(%s) is in use"
                            % (fip_addr))

        aip_pool_refs = db_vn_dict.get('alias_ip_pools', [])
        for ref in aip_pool_refs:
            try:
                (ok, result) = db_conn.dbe_read(
                    'alias_ip_pool', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue
            if not ok:
                self.config_log(
                    "Error in subnet delete alias-ip-pool check: %s"
                        %(result),
                    level=SandeshLevel.SYS_ERR)
                return False, result

            alias_ips = result.get('alias_ips', [])
            for alias_ip in alias_ips:
                # get alias_ip_address and this should be in
                # new subnet_list
                try:
                    (read_ok, read_result) = db_conn.dbe_read(
                        'alias_ip', {'uuid': floating_ip['uuid']})
                except cfgm_common.exceptions.NoIdError:
                    continue
                if not read_ok:
                    self.config_log(
                        "Error in subnet delete floating-ip check: %s"
                            %(read_result),
                        level=SandeshLevel.SYS_ERR)
                    return False, result

                aip_addr = read_result.get('alias_ip_address')
                if not aip_addr:
                    self.config_log(
                        "Error in subnet delete aip null: %s"
                        %(alias_ip['uuid']),
                        level=SandeshLevel.SYS_ERR)
                    continue
                if not all_matching_cidrs(aip_addr, requested_subnets):
                    return (False,
                            "Cannot Delete IP Block, Floating IP(%s) is in use"
                            % (aip_addr))

        return True, ""
    # end net_check_subnet_delete

    # return number of ip address currently allocated for a subnet
    # count will also include reserved ips
    def ip_count_req(self, vn_fq_name, subnet_name=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
        return subnet_obj.ip_count()
    # end ip_count_req

    # allocate an IP address for given virtual network
    # we use the first available subnet unless provided
    def ip_alloc_req(self, vn_fq_name, vn_dict=None, sub=None, asked_ip_addr=None,
                     asked_ip_version=4, alloc_id=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_dicts = self._get_subnet_dicts(vn_fq_name, vn_dict)

        if not subnet_dicts:
            raise AddrMgmtSubnetUndefined(vn_fq_name_str)

        current_count = 0
        subnet_count = len(subnet_dicts)
        for subnet_name in subnet_dicts:
            current_count += 1
            if sub and sub != subnet_name:
                continue

            # create subnet_obj internally if it was created by some other
            # api-server before
            try:
                subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
            except KeyError:
                if vn_fq_name_str not in self._subnet_objs:
                    self._subnet_objs[vn_fq_name_str] = {}

                subnet_dict = subnet_dicts[subnet_name]
                subnet_obj = Subnet('%s:%s' % (vn_fq_name_str,
                                               subnet_name),
                                    subnet_dict['ip_prefix'],
                                    subnet_dict['ip_prefix_len'],
                                    gw=subnet_dict['gw'],
                                    service_address=subnet_dict['dns_server_address'],
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'],
                                    should_persist=False,
                                    ip_alloc_unit=subnet_dict['alloc_unit'])
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if asked_ip_version and asked_ip_version != subnet_obj.get_version():
                continue
            if asked_ip_addr == str(subnet_obj.gw_ip):
                return asked_ip_addr
            if asked_ip_addr == str(subnet_obj.dns_server_address):
                return asked_ip_addr
            if asked_ip_addr and not subnet_obj.ip_belongs(asked_ip_addr):
                continue

            # if user requests ip-addr and that can't be reserved due to
            # existing object(iip/fip) using it, return an exception with
            # the info. client can determine if its error or not
            if asked_ip_addr:
                if (int(IPAddress(asked_ip_addr)) % subnet_obj.alloc_unit):
                    raise AddrMgmtAllocUnitInvalid(
                        subnet_obj._name,
                        subnet_obj._prefix+'/'+subnet_obj._prefix_len,
                        subnet_obj.alloc_unit)

                return subnet_obj.ip_reserve(ipaddr=asked_ip_addr,
                                             value=alloc_id)

            try:
                ip_addr = subnet_obj.ip_alloc(ipaddr=None,
                                              value=alloc_id)
            except cfgm_common.exceptions.ResourceExhaustionError as e:
                # ignore exception if it not a last subnet
                self.config_log("In ip_alloc_req: %s" %(str(e)),
                                level=SandeshLevel.SYS_DEBUG)
                if current_count < subnet_count:
                    continue
                else:
                    raise AddrMgmtSubnetExhausted(vn_fq_name, 'all')

            if ip_addr is not None or sub:
                return ip_addr

        raise AddrMgmtSubnetExhausted(vn_fq_name, 'all')
    # end ip_alloc_req

    def ip_alloc_notify(self, ip_addr, vn_fq_name):
        vn_fq_name_str = ':'.join(vn_fq_name)
        try:
            subnet_dicts = self._get_subnet_dicts(vn_fq_name)
        except cfgm_common.exceptions.NoIdError:
            return

        for subnet_name in subnet_dicts:
            # create subnet_obj internally if it was created by some other
            # api-server before
            try:
                subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
            except KeyError:
                if vn_fq_name_str not in self._subnet_objs:
                    self._subnet_objs[vn_fq_name_str] = {}

                subnet_dict = subnet_dicts[subnet_name]
                subnet_obj = Subnet('%s:%s' % (vn_fq_name_str,
                                               subnet_name),
                                    subnet_dict['ip_prefix'],
                                    subnet_dict['ip_prefix_len'],
                                    gw=subnet_dict['gw'],
                                    service_address=subnet_dict['dns_server_address'],
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'],
                                    should_persist=False,
                                    ip_alloc_unit=subnet_dict['alloc_unit'])
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if not subnet_obj.ip_belongs(ip_addr):
                continue

            ip_addr = subnet_obj.ip_set_in_use(ipaddr=ip_addr)
            break
    # end ip_alloc_notify

    def ip_free_req(self, ip_addr, vn_fq_name, sub=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_dicts = self._get_subnet_dicts(vn_fq_name)
        for subnet_name in subnet_dicts:
            if sub and sub != subnet_name:
                continue

            # if we have subnet_obj free it via instance method,
            # updating inuse bitmask, else free it via class method
            # and there is no inuse bitmask to worry about
            try:
                subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
            except KeyError:
                if vn_fq_name_str not in self._subnet_objs:
                    self._subnet_objs[vn_fq_name_str] = {}

                subnet_dict = subnet_dicts[subnet_name]
                subnet_obj = Subnet('%s:%s' % (vn_fq_name_str,
                                               subnet_name),
                                    subnet_dict['ip_prefix'],
                                    subnet_dict['ip_prefix_len'],
                                    gw=subnet_dict['gw'],
                                    service_address=subnet_dict['dns_server_address'],
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'],
                                    should_persist=False,
                                    ip_alloc_unit=subnet_dict['alloc_unit'])
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if Subnet.ip_belongs_to(IPNetwork(subnet_name),
                                    IPAddress(ip_addr)):
                subnet_obj.ip_free(IPAddress(ip_addr))
                break
    # end ip_free_req

    def is_ip_allocated(self, ip_addr, vn_fq_name, sub=None):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_dicts = self._get_subnet_dicts(vn_fq_name)
        for subnet_name in subnet_dicts:
            if sub and sub != subnet_name:
                continue

            # if we have subnet_obj free it via instance method,
            # updating inuse bitmask, else free it via class method
            # and there is no inuse bitmask to worry about
            try:
                subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
            except KeyError:
                if vn_fq_name_str not in self._subnet_objs:
                    self._subnet_objs[vn_fq_name_str] = {}

                subnet_dict = subnet_dicts[subnet_name]
                subnet_obj = Subnet('%s:%s' % (vn_fq_name_str,
                                               subnet_name),
                                    subnet_dict['ip_prefix'],
                                    subnet_dict['ip_prefix_len'],
                                    gw=subnet_dict['gw'],
                                    service_address=subnet_dict['dns_server_address'],
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'],
                                    should_persist=False)
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if Subnet.ip_belongs_to(IPNetwork(subnet_name),
                                    IPAddress(ip_addr)):
                return subnet_obj.is_ip_allocated(IPAddress(ip_addr))
    # end is_ip_allocated

    def ip_free_notify(self, ip_addr, vn_fq_name):
        vn_fq_name_str = ':'.join(vn_fq_name)
        for subnet_name in self._subnet_objs.get(vn_fq_name_str) or []:
            subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
            if Subnet.ip_belongs_to(IPNetwork(subnet_name),
                                    IPAddress(ip_addr)):
                subnet_obj.ip_reset_in_use(ip_addr)
                break
    # end ip_free_notify

    def mac_alloc(self, obj_dict):
        uid = obj_dict['uuid']
        return '02:%s:%s:%s:%s:%s' % (uid[0:2], uid[2:4], uid[4:6],
                                      uid[6:8], uid[9:11])
    # end mac_alloc

# end class AddrMgmt
