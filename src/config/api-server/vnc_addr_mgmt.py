#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from netaddr import *
from vnc_quota import *
from pprint import pformat
from copy import deepcopy
import json
import cfgm_common.exceptions
try:
    #python2.7
    from collections import OrderedDict
except:
    #python2.6
    from ordereddict import OrderedDict

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
                 gw=None, enable_dhcp=True,
                 dns_nameservers=None,
                 alloc_pool_list=None,
                 addr_from_start=False):

        """
        print 'Name = %s, prefix = %s, len = %s, gw = %s, db_conn = %s' \
            % (name, prefix, prefix_len, gw, 'Yes' if db_conn else 'No')
        """

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

        # check dns_nameservers
        for nameserver in dns_nameservers or []:
            try:
                ip_addr = IPAddress(nameserver)
            except AddrFormatError:
                raise AddrMgmtInvalidDnsServer(name, nameserver)

        # Exclude host and broadcast
        exclude = [IPAddress(network.first), network.broadcast]

        # if allocation-pool is not specified, create one with entire cidr
        if not alloc_pool_list:
            alloc_pool_list = [{'start':str(IPAddress(network.first)),
                                'end':str(IPAddress(network.last-1))}]

        # need alloc_pool_list with integer to use in Allocator
        alloc_int_list = list()

        #store integer for given ip address in allocation list
        for alloc_pool in alloc_pool_list:
            alloc_int = {'start':int(IPAddress(alloc_pool['start'])),
                         'end':int(IPAddress(alloc_pool['end']))}
            alloc_int_list.append(alloc_int)

        # exclude gw_ip if it is within allocation-pool
        for alloc_int in alloc_int_list:
            if alloc_int['start'] <= int(gw_ip) <= alloc_int['end']:
                exclude.append(gw_ip)
                break
        self._db_conn.subnet_create_allocator(name, alloc_int_list,
                                              addr_from_start)

        # reserve excluded addresses
        for addr in exclude:
            self._db_conn.subnet_alloc_req(name, int(addr))

        self._name = name
        self._network = network
        self._version = network.version
        self._exclude = exclude
        self.gw_ip = gw_ip
        self._alloc_pool_list = alloc_pool_list
        self.enable_dhcp = enable_dhcp
        self.dns_nameservers = dns_nameservers 
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

    def is_ip_allocated(self, ipaddr):
        ip = IPAddress(ipaddr)
        addr = int(ip)
        return self._db_conn.subnet_is_addr_allocated(self._name, addr)
    # end is_ip_allocated

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
        subnet_dicts = OrderedDict()
        for ipam_ref in ipam_refs:
            vnsn_data = ipam_ref['attr']
            ipam_subnets = vnsn_data['ipam_subnets']
            for ipam_subnet in ipam_subnets:
                subnet_dict = copy.deepcopy(ipam_subnet['subnet'])
                subnet_dict['gw'] = ipam_subnet.get('default_gateway', None)
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
                    allocation_pools = ipam_subnet.get('allocation_pools', None)
                    dhcp_config = ipam_subnet.get('enable_dhcp', True)
                    nameservers = ipam_subnet.get('dns_nameservers', None)
                    addr_start = ipam_subnet.get('addr_from_start', False)
                    subnet_obj = Subnet(
                        '%s:%s' % (vn_fq_name_str, subnet_name),
                        subnet['ip_prefix'], str(subnet['ip_prefix_len']),
                        gw=gateway_ip, enable_dhcp=dhcp_config,
                        dns_nameservers=nameservers,
                        alloc_pool_list=allocation_pools,
                        addr_from_start=addr_start)
                    self._subnet_objs[vn_fq_name_str][subnet_name] = \
                         subnet_obj
                    ipam_subnet['default_gateway'] = str(subnet_obj.gw_ip)
    # end _create_subnet_objs

    def net_create_req(self, obj_dict):
        self._get_db_conn()
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

        # check db_subnet_dicts and req_subnet_dicts  
        # following parameters are same for subnets present in both dicts
        # 1. enable_dhcp, 2. default_gateway, 3. allocation_pool 
        # 4 dns_nameservers
        for key in req_subnet_dicts.keys():
            req_subnet = req_subnet_dicts[key]
            if key in db_subnet_dicts.keys():
                db_subnet = db_subnet_dicts[key]
                if req_subnet['enable_dhcp'] is None:
                    req_subnet['enable_dhcp'] = True
                if (req_subnet['gw'] != db_subnet['gw']):
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)

                req_alloc_list = req_subnet['allocation_pools'] or []
                db_alloc_list = db_subnet['allocation_pools'] or []
                if (len(req_alloc_list) != len(db_alloc_list)):
                    raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)

                for index in range(len(req_alloc_list)):
                    if cmp(req_alloc_list[index], db_alloc_list[index]):
                        raise AddrMgmtSubnetInvalid(vn_fq_name_str, key)

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
        self._create_subnet_objs(vn_fq_name_str, vn_dict)
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
        subnets = self._vn_to_subnets(req_vn_dict)
        if not subnets:
            return True, ""
        proj_uuid = db_vn_dict['parent_uuid']
        (ok, proj_dict) = QuotaHelper.get_project_dict(proj_uuid, db_conn)
        if not ok:
            return (False, 'Internal error : ' + pformat(proj_dict))

        obj_type = 'subnet'
        for network in proj_dict.get('virtual_networks', []):
            if network['uuid'] == db_vn_dict['uuid']:
                continue
            ok, net_dict = db_conn.dbe_read('virtual-network', network)
            if not ok:
                continue
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
    def net_check_subnet_overlap(self, db_vn_dict, req_vn_dict):
        # get all subnets existing + requested and check any non-exact overlaps
        requested_subnets = self._vn_to_subnets(req_vn_dict)
        if not requested_subnets:
            return True, ""

        existing_subnets = self._vn_to_subnets(db_vn_dict)
        if not existing_subnets:
            existing_subnets = []

        # literal/string sets
        # eg. existing [1.1.1.0/24],
        #     requested [1.1.1.0/24, 2.2.2.0/24] OR
        #     requested [1.1.1.0/16, 2.2.2.0/24]
        existing_set = set([sn for sn in existing_subnets])
        requested_set = set([sn for sn in requested_subnets])
        new_set = requested_set - existing_set

        # IPSet to find any overlapping subnets
        overlap_set = IPSet(existing_set) & IPSet(new_set)
        if overlap_set:
            err_msg = "Overlapping addresses between requested and existing: "
            return False, err_msg + str(overlap_set)

        return True, ""
    # end net_check_subnet_overlap

    def net_check_subnet(self, db_vn_dict, req_vn_dict):
        ipam_refs = req_vn_dict.get('network_ipam_refs', [])
        for ipam_ref in ipam_refs:
            vnsn_data = ipam_ref['attr']
            ipam_subnets = vnsn_data['ipam_subnets']
            l2_mode = False
            l2_l3_mode = False
            link_local_network = IPNetwork('169.254.0.0/16')
            for ipam_subnet in ipam_subnets:
                subnet_dict = copy.deepcopy(ipam_subnet['subnet'])
                prefix = subnet_dict['ip_prefix']
                prefix_len = subnet_dict['ip_prefix_len']
                network = IPNetwork('%s/%s' % (prefix, prefix_len))
                subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                    subnet_dict['ip_prefix_len'])

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
                    # gateway being 0.0.0.0 or 169.254.0.0/16 results in
                    # vn being spawned in l2 forwarding mode
                    if gw_ip == IPAddress('0.0.0.0') or \
                       (gw_ip >= IPAddress(link_local_network.first) and \
                       gw_ip <= IPAddress(link_local_network.last)):
                        l2_mode = True
                    elif gw_ip < IPAddress(network.first + 1) or \
                         gw_ip > IPAddress(network.last - 1):
                        err_msg = "gateway Ip %s out of cidr: %s" \
                            %(gw, subnet_name)
                        return False, err_msg
                    else:
                        l2_l3_mode = True

                # Either all subnet will have gateway as required for l2 mode
                # or for l2_l3 mode. Both can not co-exist as vn forwarding mode
                # becomes ambiguous
                if l2_mode and l2_l3_mode:
                    err_msg = "gateway Ip of configured subnets conflicting " \
                        "in deciding forwarding mode for virtual network."
                    return False, err_msg
        return True, ""
    # end net_check_subnet

    # check subnets associated with a virtual network, return error if
    # any subnet is being deleted and has backref to instance-ip/floating-ip
    def net_check_subnet_delete(self, db_vn_dict, req_vn_dict):
        db_conn = self._get_db_conn()
        # if all instance-ip/floating-ip are part of requested list
        # things are ok.
        # eg. existing [1.1.1.0/24, 2.2.2.0/24],
        #     requested [1.1.1.0/24] OR
        #     requested [1.1.1.0/28, 2.2.2.0/24]
        requested_subnets = self._vn_to_subnets(req_vn_dict)
        if requested_subnets == None:
            # subnets not modified in request
            return True, ""

        # if all subnets are being removed, check for any iip backrefs
        # or floating pools still present in DB version of VN
        if len(requested_subnets) == 0:
            if db_vn_dict.get('instance_ip_back_refs'):
                return False, "Cannot Delete IP Block, Instance IP(s) in use"
            if db_vn_dict.get('floating_ip_pools'):
                return False, "Cannot Delete IP Block, Floating Pool(s) in use"

        instip_refs = db_vn_dict.get('instance_ip_back_refs', [])
        for ref in instip_refs:
            try:
                (ok, result) = db_conn.dbe_read(
                    'instance-ip', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue

            if not ok:
                continue

            inst_ip = result.get('instance_ip_address', None)
            if not all_matching_cidrs(inst_ip, requested_subnets):
                return False,\
                    "Cannot Delete IP Block, IP(%s) is in use"\
                    % (inst_ip)

        fip_pool_refs = db_vn_dict.get('floating_ip_pools', [])
        for ref in fip_pool_refs:
            try:
                (ok, result) = db_conn.dbe_read(
                    'floating-ip-pool', {'uuid': ref['uuid']})
            except cfgm_common.exceptions.NoIdError:
                continue

            if not ok:
                continue

            floating_ips = result.get('floating_ips', [])
            for floating_ip in floating_ips:
                # get floating_ip_address and this should be in
                # new subnet_list
                try:
                    (read_ok, read_result) = db_conn.dbe_read(
                        'floating-ip', {'uuid': floating_ip['uuid']})
                except cfgm_common.exceptions.NoIdError:
                    continue

                if not ok:
                    continue

                fip_addr = read_result.get('floating_ip_address', None)
                if not all_matching_cidrs(fip_addr, requested_subnets):
                    return False,\
                        "Cannot Delete IP Block, Floating IP(%s) is in use"\
                        % (fip_addr)

        return True, ""
    # end net_check_subnet_delete

    # allocate an IP address for given virtual network
    # we use the first available subnet unless provided
    def ip_alloc_req(self, vn_fq_name, sub=None, asked_ip_addr=None, 
                     asked_ip_version=4):
        vn_fq_name_str = ':'.join(vn_fq_name)
        subnet_dicts = self._get_subnet_dicts(vn_fq_name)

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
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'])
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if asked_ip_version != subnet_obj.get_version():
                continue
            if asked_ip_addr and not subnet_obj.ip_belongs(asked_ip_addr):
                continue
            try:
                ip_addr = subnet_obj.ip_alloc(ipaddr=asked_ip_addr)
            except Exception as e:
                # ignore exception if it not a last subnet
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
        subnet_dicts = self._get_subnet_dicts(vn_fq_name)
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
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'])
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if not subnet_obj.ip_belongs(ip_addr):
                continue

            ip_addr = subnet_obj.ip_alloc(ipaddr=ip_addr)
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
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'])
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
                                    enable_dhcp=subnet_dict['enable_dhcp'],
                                    dns_nameservers=subnet_dict['dns_nameservers'],
                                    alloc_pool_list=subnet_dict['allocation_pools'],
                                    addr_from_start = subnet_dict['addr_start'])
                self._subnet_objs[vn_fq_name_str][subnet_name] = subnet_obj

            if Subnet.ip_belongs_to(IPNetwork(subnet_name),
                                    IPAddress(ip_addr)):
                return subnet_obj.is_ip_allocated(IPAddress(ip_addr))
    # end is_ip_allocated

    def ip_free_notify(self, ip_addr, vn_fq_name):
        vn_fq_name_str = ':'.join(vn_fq_name)
        for subnet_name in self._subnet_objs[vn_fq_name_str]:
            subnet_obj = self._subnet_objs[vn_fq_name_str][subnet_name]
            if Subnet.ip_belongs_to(IPNetwork(subnet_name),
                                    IPAddress(ip_addr)):
                subnet_obj.ip_free(IPAddress(ip_addr))
                break
    # end ip_free_notify

    # Given IP address count on given virtual network, subnet/List of subnet
    def ip_count(self, obj_dict, subnet=None):
        db_conn = self._get_db_conn()
        addr_num = 0
        if not subnet:
            return addr_num

        instip_refs = obj_dict.get('instance_ip_back_refs', None)
        if instip_refs:
            for ref in instip_refs:
                uuid = ref['uuid']
                try:
                    (ok, result) = db_conn.dbe_read(
                        'instance-ip', {'uuid': uuid})
                except cfgm_common.exceptions.NoIdError:
                    continue
                if not ok:
                    continue

                inst_ip = result.get('instance_ip_address', None)
                if IPAddress(inst_ip) in IPNetwork(subnet):
                    addr_num += 1

        return addr_num
    # end ip_count

    def mac_alloc(self, obj_dict):
        uid = obj_dict['uuid']
        return '02:%s:%s:%s:%s:%s' % (uid[0:2], uid[2:4], uid[4:6],
                                      uid[6:8], uid[9:11])
    # end mac_alloc

# end class AddrMgmt
