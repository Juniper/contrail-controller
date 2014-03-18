import re
from verification_util import *

class CsDomainResult (Result):
    '''
        CsDomainResult to provide access to vnc_introspect_utils.get_cs_domain
        dict contrains:

        {u'domain': {
             u'fq_name': [u'ted-domain'],
             u'id_perms': {u'created': None,
                            u'enable': True,
                            u'last_modified': None,
                            u'permissions': {u'group': u'cloud-admin-group',
                                             u'group_access': 7,
                                             u'other_access': 7,
                                             u'owner': u'cloud-admin',
                                             u'owner_access': 7},
                            u'uuid': {u'uuid_lslong': 13068984139654137108L,
                                      u'uuid_mslong': 9504116366942620127L}},
             u'namespaces': [{u'attr': {},
                               u'href': u'http://10.84.7.4:8082/namespace/c0552b1f-588e-4507-8962-b1837c8f883a',
                               u'to': [u'ted-domain', u'default-namespace'],
                               u'uuid': u'c0552b1f-588e-4507-8962-b1837c8f883a'}],
             u'projects': [{u'attr': {},
                             u'href': u'http://10.84.7.4:8082/project/0d779509-7d54-4842-9b34-f85557898b67',
                             u'to': [u'ted-domain', u'ted-eng'],
                             u'uuid': u'0d779509-7d54-4842-9b34-f85557898b67'},
                            {u'attr': {},
                             u'href': u'http://10.84.7.4:8082/project/1fcf3244-d4d9-407d-8637-54bb2522020e',
                             u'to': [u'ted-domain', u'default-project'],
                             u'uuid': u'1fcf3244-d4d9-407d-8637-54bb2522020e'}],
             u'_type': u'domain',
             u'href': u'http://10.84.7.4:8082/domain/83e5677b-1397-49df-b55e-5bd5234c8514',
             u'name': u'ted-domain',
             u'uuid': u'83e5677b-1397-49df-b55e-5bd5234c8514'}}

    '''
    def fq_name (self):
        return ':'.join (self.xpath ('domain', 'fq_name'))

    def name (self):
        return self.xpath ('domain', 'name')

    def uuid (self):
        return self.xpath ('domain', 'uuid')

    def project_list (self):
        return map(lambda x: ':'.join (x['to']),
                self.xpath ('domain', 'projects'))

    def project (self, name):
        return filter(lambda x: x['to'] == [self.name (), name],
                self.xpath ('domain', 'projects'))

    def st_list (self):
        return self.xpath('domain', 'service_templates')

    def st(self, st):
        return filter(lambda x: x['to'][-1] == st, self.st_list())

    def vdns_list(self):
        return self.xpath('domain','virtual_DNSs')
    def vdns(self,vdns_name):
        vdns_li = self.vdns_list()
        if vdns_li:
            return filter(lambda x: x['to'][-1] == vdns_name, vdns_li)


class CsProjectResult (Result):
    '''
        CsDomainResult to provide access to vnc_introspect_utils.get_cs_project
        dict contrains:

        {u'project': {u'fq_name': [u'ted-domain', u'ted-eng'],
                      u'id_perms': {u'created': None,
                          u'enable': True,
                          u'last_modified': None,
                          u'permissions': {u'group': u'cloud-admin-group',
                                           u'group_access': 7,
                                           u'other_access': 7,
                                           u'owner': u'cloud-admin',
                                           u'owner_access': 7},
                          u'uuid': {u'uuid_lslong': 11183836820092324711L,
                                    u'uuid_mslong': 970408112711551042}},
                      u'network_ipams': [{u'attr': {},
                                           u'href': u'http://10.84.7.4:8082/network-ipam/52310151-ec68-4052-9114-14ae1a47f2fb',
                                           u'to': [u'ted-domain',
                                                   u'ted-eng',
                                                   u'default-network-ipam'],
                                           u'uuid': u'52310151-ec68-4052-9114-14ae1a47f2fb'}],
                      u'network_policys': [{u'attr': {},
                                             u'href': u'http://10.84.7.4:8082/network-policy/c30461ae-e72a-44a6-845b-7510c7ae3897',
                                             u'to': [u'ted-domain',
                                                     u'ted-eng',
                                                     u'default-network-policy'],
                                             u'uuid': u'c30461ae-e72a-44a6-845b-7510c7ae3897'}],
                      u'security_groups': [{u'attr': {},
                                             u'href': u'http://10.84.7.4:8082/security-group/32dc02af-1b3c-4baa-a6eb-3c97cbdd2941',
                                             u'to': [u'ted-domain',
                                                     u'ted-eng',
                                                     u'default-security-group'],
                                             u'uuid': u'32dc02af-1b3c-4baa-a6eb-3c97cbdd2941'}],
                      u'service_templates': [{u'attr': {},
                                               u'href': u'http://10.84.7.4:8082/service-template/4264dd1e-d312-4e03-a60e-35b40da39e95',
                                               u'to': [u'ted-domain',
                                                       u'ted-eng',
                                                       u'default-service-template'],
                                               u'uuid': u'4264dd1e-d312-4e03-a60e-35b40da39e95'}],
                      u'_type': u'project',
                      u'virtual_networks': [{u'attr': {},
                                              u'href': u'http://10.84.7.4:8082/virtual-network/6a5c5c29-cfe6-4fea-9768-b0dea3b217bc',
                                              u'to': [u'ted-domain',
                                                      u'ted-eng',
                                                      u'ted-back'],
                                              u'uuid': u'6a5c5c29-cfe6-4fea-9768-b0dea3b217bc'},
                                             {u'attr': {},
                                              u'href': u'http://10.84.7.4:8082/virtual-network/926c8dcc-0b8b-444f-9f59-9ab67a8f9f48',
                                              u'to': [u'ted-domain',
                                                      u'ted-eng',
                                                      u'ted-front'],
                                              u'uuid': u'926c8dcc-0b8b-444f-9f59-9ab67a8f9f48'},
                                             {u'attr': {},
                                              u'href': u'http://10.84.7.4:8082/virtual-network/b312647f-0921-4ddf-9d59-0667a887989f',
                                              u'to': [u'ted-domain',
                                                      u'ted-eng',
                                                      u'default-virtual-network'],
                                              u'uuid': u'b312647f-0921-4ddf-9d59-0667a887989f'}],
                      u'href': u'http://10.84.7.4:8082/project/0d779509-7d54-4842-9b34-f85557898b67',
                      u'name': u'ted-eng',
                      u'parent_name': u'ted-domain',
                      u'uuid': u'0d779509-7d54-4842-9b34-f85557898b67'}}
    '''
    def fq_name (self):
        return ':'.join (self.xpath ('project', 'fq_name'))

    def policy_list (self):
        return self.xpath ('project', 'network_policys')

    def policy (self, policy):
        return filter(lambda x: x['to'][-1] == policy, self.policy_list ())

    def vn_list (self):
        return self.xpath ('project', 'virtual_networks')

    def vn (self, vn):
        if self.vn_list():
            return filter(lambda x: x['to'][-1] == vn, self.vn_list ())
        return []

    def fip_list (self):
        if self.has_key ('floating_ip_pool_refs'):
            p = self.xpath ('project', 'floating_ip_pool_refs')
        else:
            p = []
        return p

    def fip (self, fip_fq_name=[]):
        return filter(lambda x: x['to'] == fip_fq_name, self.fip_list ())

    def secgrp_list (self):
        return self.xpath('project', 'security_groups')

    def secgrp(self, secgrp):
        secgrp_list = self.secgrp_list()
        if secgrp_list:
            return filter(lambda x: x['to'][-1] == secgrp, secgrp_list)

    def si_list (self):
        return self.xpath('project', 'service_instances')

    def si(self, si):
        si_list =  self.si_list()
        if si_list:
            return filter(lambda x: x['to'][-1] == si, si_list)

class CsVdnsResult(Result):     
    def fq_name (self):
        return ':'.join (self.xpath ('virtual-DNS', 'fq_name'))
    def vdns_data(self):
        return ':'.join (self.xpath ('virtual-DNS','virtual_DNS_data'))
    def vdns_records(self):
        return ':'.join (self.xpath ('virtual-DNS','virtual_DNS_records'))
#end of CsVdnsResult


class CsUseFipResult (Result):
    '''
        CsUseFipResult to provide access to vnc_introspect_utils.get_cs_use_fip_pool
        dict contrains:

{u'floating-ip-pool': {u'fq_name': [u'ted-domain',
                                     u'ted-eng',
                                     u'ted-front',
                                     u'ted_fip_pool'],
                       u'id_perms': {u'created': None,
                                      u'enable': True,
                                      u'last_modified': None,
                                      u'permissions': {u'group': u'cloud-admin-group',
                                                       u'group_access': 7,
                                                       u'other_access': 7,
                                                       u'owner': u'cloud-admin',
                                                       u'owner_access': 7},
                                      u'uuid': {u'uuid_lslong': 13214437371555268939L,
                                                u'uuid_mslong': 18023639221065174839L}},
                       u'project_back_refs': [{u'attr': {},
                                                u'href': u'http://10.84.7.4:8082/project/1fcf3244-d4d9-407d-8637-54bb2522020e',
                                                u'to': [u'ted-domain',
                                                        u'default-project'],
                                                u'uuid': u'1fcf3244-d4d9-407d-8637-54bb2522020e'}],
                       u'_type': u'floating-ip-pool',
                       u'href': u'http://10.84.7.4:8082/floating-ip-pool/fa20d460-d363-4f37-b763-1cc6be32c94b',
                       u'name': u'ted_fip_pool',
                       u'parent_name': u'ted-front',
                       u'uuid': u'fa20d460-d363-4f37-b763-1cc6be32c94b'}}
    '''


class CsAllocFipResult (Result):
    '''
        CsAllocFipResult to provide access to vnc_introspect_utils.get_cs_alloc_fip_pool
        dict contrains:

{u'floating-ip-pool': {u'fq_name': [u'ted-domain',
                                     u'ted-eng',
                                     u'ted-front',
                                     u'ted_fip_pool'],
                       u'id_perms': {u'created': None,
                                      u'enable': True,
                                      u'last_modified': None,
                                      u'permissions': {u'group': u'cloud-admin-group',
                                                       u'group_access': 7,
                                                       u'other_access': 7,
                                                       u'owner': u'cloud-admin',
                                                       u'owner_access': 7},
                                      u'uuid': {u'uuid_lslong': 13214437371555268939L,
                                                u'uuid_mslong': 18023639221065174839L}},
                       u'project_back_refs': [{u'attr': {},
                                                u'href': u'http://10.84.7.4:8082/project/1fcf3244-d4d9-407d-8637-54bb2522020e',
                                                u'to': [u'ted-domain',
                                                        u'default-project'],
                                                u'uuid': u'1fcf3244-d4d9-407d-8637-54bb2522020e'}],
                       u'_type': u'floating-ip-pool',
                       u'href': u'http://10.84.7.4:8082/floating-ip-pool/fa20d460-d363-4f37-b763-1cc6be32c94b',
                       u'name': u'ted_fip_pool',
                       u'parent_name': u'ted-front',
                       u'uuid': u'fa20d460-d363-4f37-b763-1cc6be32c94b'}}
    '''
    pass

class CsIPAMResult (Result):
    '''
        CsIPAMResult to provide access to vnc_introspect_utils.get_cs_ipam
        dict contrains:

    {u'network-ipam': {u'fq_name': [u'ted-domain',
                                     u'ted-eng',
                                     u'default-network-ipam'],
                       u'id_perms': {u'created': None,
                                      u'enable': True,
                                      u'last_modified': None,
                                      u'permissions': {u'group': u'cloud-admin-group',
                                                       u'group_access': 7,
                                                       u'other_access': 7,
                                                       u'owner': u'cloud-admin',
                                                       u'owner_access': 7},
                                      u'uuid': {u'uuid_lslong': 10454003373031551739L,
                                                u'uuid_mslong': 5922516436339146834}},
                       u'network_ipam_mgmt': {u'dhcp_option_list': None,
                                               u'ipam_method': u'dhcp'},
                       u'_type': u'network-ipam',
                       u'virtual_network_back_refs': [{u'attr': {u'ipam_subnets': [{u'default_gateway': None,
                                                                                     u'subnet': {u'ip_prefix': u'192.168.1.0',
                                                                                                 u'ip_prefix_len': 24}}]},
                                                        u'href': u'http://10.84.7.4:8082/virtual-network/6a5c5c29-cfe6-4fea-9768-b0dea3b217bc',
                                                        u'to': [u'ted-domain',
                                                                u'ted-eng',
                                                                u'ted-back'],
                                                        u'uuid': u'6a5c5c29-cfe6-4fea-9768-b0dea3b217bc'}],
                       u'href': u'http://10.84.7.4:8082/network-ipam/52310151-ec68-4052-9114-14ae1a47f2fb',
                       u'name': u'default-network-ipam',
                       u'parent_name': u'ted-eng',
                       u'uuid': u'52310151-ec68-4052-9114-14ae1a47f2fb'}}
    '''
    def fq_name (self):
        return ':'.join (self.xpath ('network-ipam', 'fq_name'))

class CsPolicyResult (Result):
    '''
        CsPolicyResult to provide access to vnc_introspect_utils.get_cs_policy
        dict contrains:

        {u'network-policy': {u'fq_name': [u'ted-domain',
                                           u'ted-eng',
                                           u'default-network-policy'],
                             u'id_perms': {u'created': None,
                                            u'enable': True,
                                            u'last_modified': None,
                                            u'permissions': {u'group': u'cloud-admin-group',
                                                             u'group_access': 7,
                                                             u'other_access': 7,
                                                             u'owner': u'cloud-admin',
                                                             u'owner_access': 7},
                                            u'uuid': {u'uuid_lslong': 9537345350817167511L,
                                                      u'uuid_mslong': 14052464141133300902L}},
                             u'_type': u'network-policy',
                             u'href': u'http://10.84.7.4:8082/network-policy/c30461ae-e72a-44a6-845b-7510c7ae3897',
                             u'name': u'default-network-policy',
                             u'parent_name': u'ted-eng',
                             u'uuid': u'c30461ae-e72a-44a6-845b-7510c7ae3897'}}
    '''
    def fq_name (self):
        return ':'.join (self.xpath ('network-policy', 'fq_name'))


class CsVNResult (Result):
    '''
        CsVNResult to provide access to vnc_introspect_utils.get_cs_vn
        dict contrains:

{u'virtual-network': {u'fq_name': [u'ted-domain', u'ted-eng', u'ted-back'],
                  u'id_perms': {u'created': None,
                                 u'enable': True,
                                 u'last_modified': None,
                                 u'permissions': {u'group': u'cloud-admin-group',
                                                  u'group_access': 7,
                                                  u'other_access': 7,
                                                  u'owner': u'cloud-admin',
                                                  u'owner_access': 7},
                                 u'uuid': {u'uuid_lslong': 10910164567580612540L,
                                           u'uuid_mslong': 7664102000529133546}},
                  u'instance_ip_back_refs': [{u'attr': {},
                                               u'href': u'http://10.84.7.4:8082/instance-ip/9d4cbfbc-da80-4732-a98e-77607bd78704',
                                               u'to': [u'9d4cbfbc-da80-4732-a98e-77607bd78704'],
                                               u'uuid': u'9d4cbfbc-da80-4732-a98e-77607bd78704'}],
                  u'network_ipam_refs': [{u'attr': {u'ipam_subnets': [{u'default_gateway': None,
                                                                        u'subnet': {u'ip_prefix': u'192.168.1.0',
                                                                                    u'ip_prefix_len': 24}}]},
                                           u'href': u'http://10.84.7.4:8082/network-ipam/52310151-ec68-4052-9114-14ae1a47f2fb',
                                           u'to': [u'ted-domain',
                                                   u'ted-eng',
                                                   u'default-network-ipam'],
                                           u'uuid': u'52310151-ec68-4052-9114-14ae1a47f2fb'}],
                  u'routing_instances': [{u'attr': {},
                                           u'href': u'http://10.84.7.4:8082/routing-instance/a68948af-46be-4f26-b73e-9ec725f57437',
                                           u'to': [u'ted-domain',
                                                   u'ted-eng',
                                                   u'ted-back',
                                                   u'ted-back'],
                                           u'uuid': u'a68948af-46be-4f26-b73e-9ec725f57437'}],
                  u'_type': u'virtual-network',
                  u'virtual_machine_interface_back_refs': [{u'attr': {},
                                                             u'href': u'http://10.84.7.4:8082/virtual-machine-interface/864ecd37-cf1f-43d5-9f63-4f24831859eb',
                                                             u'to': [u'c707f91f-68e9-427a-a0ba-92563c0d067f',
                                                                     u'864ecd37-cf1f-43d5-9f63-4f24831859eb'],
                                                             u'uuid': u'864ecd37-cf1f-43d5-9f63-4f24831859eb'}],
                  u'href': u'http://10.84.7.4:8082/virtual-network/6a5c5c29-cfe6-4fea-9768-b0dea3b217bc',
                  u'name': u'ted-back',
                  u'parent_name': u'ted-eng',
                  u'uuid': u'6a5c5c29-cfe6-4fea-9768-b0dea3b217bc'}}
    '''
    _pat = None
    def _rpat (self):
        if self._pat is None:
            self._pat = re.compile ('-interface/.*$')
        return self._pat

    def sub (self, st, _id):
        return self._rpat ().sub ('/%s' % _id, st)

    def fq_name (self):
        return ':'.join (self.xpath ('virtual-network', 'fq_name'))

    def fip_list (self):
        return self.xpath ('virtual-network', 'floating_ip_pools')

    def fip (self, fip):
        return filter(lambda x: x['to'][-1] == fip, self.fip_list ())

    def vm_link_list (self):
        return map(lambda x: self.sub (x['href'], x['to'][0]), 
            self.xpath ('virtual-network',
                'virtual_machine_interface_back_refs'))

    def rts (self):
        if self.xpath ('virtual-network').has_key ('route_target_list'):
            for rt in  self.xpath ('virtual-network', 'route_target_list', 
                    'route_target'):
                yield rt

    def ri_links (self):
        if self.xpath ('virtual-network').has_key ('routing_instances'):
            for ri in  self.xpath ('virtual-network', 'routing_instances'):
                yield ri['href']

    def uuid (self):
        return self.xpath ('virtual-network', 'uuid')

class CsRtResult (Result):
    '''
        CsRtResult to provide access to vnc_introspect_utils.get_cs_route_targets
        dict contrains:

    '''
    pass


class CsRiResult (Result):
    '''
        CsRiResult to provide access to vnc_introspect_utils.get_cs_routing_instances
        dict contrains:

    '''
    def rt_links (self):
        if self.xpath ('routing-instance').has_key ('route_target_refs'):
            for rt in  self.xpath ('routing-instance', 'route_target_refs'):
                yield rt['href']


class CsAllocFipPoolResult (Result):
    '''
        CsVMResult to provide access to vnc_introspect_utils.get_cs_vm
        dict contrains:

    '''
    pass



class CsVMResult (Result):
    '''
        CsVMResult to provide access to vnc_introspect_utils.get_cs_vm
        dict contrains:

    '''
    def fq_name (self):
        return ':'.join (self.xpath ('virtual-network', 'fq_name'))

    def vr_link (self):
        return self.xpath ('virtual-machine', 'virtual_router_back_refs',
                0, 'href')

    def vmi_links (self):
        vmi_list= self.xpath ('virtual-machine', 'virtual_machine_interfaces')
        links=[]
        for vmi in vmi_list : 
            links.append( vmi['href'])
        return links
#        return self.xpath ('virtual-machine', 'virtual_machine_interfaces',
#                0, 'href')

class CsVrOfVmResult (Result):
    def name (self):
        return self.xpath ('name')


class CsVmiOfVmResult (Result):
    def ip_link (self):
        return self.xpath ('virtual-machine-interface',
                'instance_ip_back_refs', 0, 'href')

    def fip_link (self):
        if self.xpath ('virtual-machine-interface').has_key (
                'floating_ip_back_refs'):
            return self.xpath ('virtual-machine-interface',
                'floating_ip_back_refs', 0, 'href')


class CsIipOfVmResult (Result):
    def ip (self):
        return self.xpath ('instance-ip', 'instance_ip_address')


class CsFipOfVmResult (Result):
    def ip (self):
        return self.xpath ('floating-ip', 'floating_ip_address')


class CsFipIdResult (Result):
    '''
        CsFipIdResult to provide access to vnc_introspect_utils.get_cs_fip
        dict contrains:

    '''
    def fip (self):
        return self.xpath ('floating-ip', 'floating_ip_address')


class CsSecurityGroupResult (Result):
    '''
        CsSecurityGroupResult to provide access to vnc_introspect_utils.get_cs_secgrp
    '''
    def fq_name (self):
        return ':'.join (self.xpath ('security-group', 'fq_name'))


class CsServiceInstanceResult (Result):
    '''
        CsServiceInstanceResult to provide access to vnc_introspect_utils.get_cs_si
    '''
    def fq_name (self):
        return ':'.join (self.xpath ('service-instance', 'fq_name'))


class CsServiceTemplateResult (Result):
    '''
        CsServiceTemplateResult to provide access to vnc_introspect_utils.get_cs_st
    '''
    def fq_name (self):
        return ':'.join (self.xpath ('service-template', 'fq_name'))
