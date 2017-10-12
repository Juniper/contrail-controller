import sys
import json
import uuid
import logging

from testtools.matchers import Equals, Contains, Not
from testtools import content, content_type, ExpectedException
import webtest.app

from vnc_api.vnc_api import *

sys.path.append('../common/tests')
from test_utils import *
import test_common

import test_case

logger = logging.getLogger(__name__)

class TestStrictCompOn(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestStrictCompOn, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'strict_compliance', True)])
    #end setUpClass

    def _create_resource(self, res_type, proj_id, name=None, extra_res_fields=None):
        proj_id = proj_id.replace('-', '')
        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': False,
                   'roles': '',
                   'tenant_id': proj_id}
        if name:
            res_name = name
        else:
            res_name = '%s-%s' %(res_type, str(uuid.uuid4()))
        data = {'resource': {'name': res_name,
                             'tenant_id': proj_id}}
        if extra_res_fields:
            data['resource'].update(extra_res_fields)

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return res_name, res_q
    # end _create_resource

    def _create_floatingip_and_associate_port_without_ext_gw(self, proj_id, name=None):
        #external network
        net_name, net_q = self._create_resource('network', proj_id, extra_res_fields={'router:external':True})
        subnet_name, subnet_q = self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': net_q['id'], 'cidr': '10.2.0.0/24', 'ip_version': 4})
        
        #private network
        pvt_net_name, pvt_net_q = self._create_resource('network', proj_id)
        pvt_subnet_name, pvt_subnet_q = self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': pvt_net_q['id'], 'cidr': '10.1.0.0/24', 'ip_version': 4})
        
        port_name, port_q = self._create_resource('port', proj_id, name, extra_res_fields={'network_id': pvt_subnet_q['network_id']})
        
        return self._create_resource('floatingip', proj_id, name, extra_res_fields={'floating_network_id': net_q['id'], 'port_id':port_q['id']})

    def _create_floatingip_and_associate_port_with_ext_gw(self, proj_id, name=None):
        #external network
        net_name, net_q = self._create_resource('network', proj_id, extra_res_fields={'router:external':True})
        subnet_name, subnet_q = self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': net_q['id'], 'cidr': '10.2.0.0/24', 'ip_version': 4})
        router_name, router_q = self._create_resource('router',proj_id, name)
        
        #private network
        pvt_net_name, pvt_net_q = self._create_resource('network', proj_id)
        pvt_subnet_name, pvt_subnet_q = self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': pvt_net_q['id'], 'cidr': '10.1.0.0/24', 'ip_version': 4})
        
        port_name, port_q = self._create_resource('port', proj_id, name, extra_res_fields={'network_id': pvt_subnet_q['network_id']})
        port2_name, port2_q = self._create_resource('port', proj_id, name, extra_res_fields={'network_id': pvt_subnet_q['network_id']})
       
        #External gateway
        router_name, router_q = self._update_resource('router', router_q['id'], proj_id, name, extra_res_fields={'external_gateway_info': {'network_id':net_q['id']}})
        router_name, router_q = self._add_router_interface('router', router_q['id'], proj_id, name, extra_res_fields={'port_id':port2_q['id']})
        return self._create_resource('floatingip', proj_id, name, extra_res_fields={'floating_network_id': net_q['id'], 'port_id':port_q['id']})


    def _update_resource(self, res_type, res_id, proj_id, name=None, extra_res_fields=None):
        proj_id = proj_id.replace('-', '')
        context = {'operation': 'UPDATE',
                   'user_id': '',
                   'is_admin': False,
                   'roles': '',
                   'tenant_id': proj_id}
        if name:
            res_name = name
        else:
            res_name = '%s-%s' %(res_type, str(uuid.uuid4()))

        data = {'resource': {'name': res_name,
                             'tenant_id': proj_id},
                'id': res_id}
        if extra_res_fields:
            data['resource'].update(extra_res_fields)

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return res_name, res_q
    # end _update_resource

    def _add_router_interface(self, res_type, res_id, proj_id, name=None, extra_res_fields=None):
        context = {'operation': 'ADDINTERFACE',
                   'user_id': '',
                   'is_admin': False,
                   'roles': '',
                   'tenant_id': proj_id}
        if name:
            res_name = name
        else:
            res_name = '%s-%s' %(res_type, str(uuid.uuid4()))

        data = {'resource': {'name': res_name,
                             'tenant_id': proj_id},
                'id': res_id}
        if extra_res_fields:
            data['resource'].update(extra_res_fields)

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return res_name, res_q
    # end _update_resource

    #test when strict_compliance is ON
    def test_create_fip_and_associate_port_without_ext_gw(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        for res_type in ['security_group']:
            res_name, res_q = getattr(self, '_create_' + res_type, lambda x:self._create_resource(res_type, x))(proj_obj.uuid)
            res_list = self._list_resources(res_type, proj_id=proj_obj.uuid, name=res_name)

        with ExpectedException(webtest.app.AppError):
            self._create_floatingip_and_associate_port_without_ext_gw(proj_obj.uuid)
 
    #test when strict_compliance is ON
    def test_create_fip_and_associate_port_with_ext_gw(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        for res_type in ['security_group']:
            res_name, res_q = getattr(self, '_create_' + res_type, lambda x:self._create_resource(res_type, x))(proj_obj.uuid)
            res_list = self._list_resources(res_type, proj_id=proj_obj.uuid, name=res_name)

        self._create_floatingip_and_associate_port_with_ext_gw(proj_obj.uuid)

    def _list_resources(self, res_type, fields=None, proj_id=None, name=None):
        proj_id = proj_id.replace('-', '')
        context = {'operation': 'READALL',
                   'userid': '',
                   'roles': '',
                   'is_admin': False,
                   'tenant': proj_id,
                   'tenant_id': proj_id}

        data = {'filters':{}, 'fields': None}
        if name:
            data.update({'filters': {'name': [name]}})
        if fields:
            data.update({'fields': fields})

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return res_q
    # end _list_resources

# end class TestStrictCompON


class TestStrictCompOff(test_case.NeutronBackendTestCase):
    @classmethod
    def setUpClass(cls):
        super(TestStrictCompOff, cls).setUpClass(
            extra_config_knobs=[('NEUTRON', 'strict_compliance', False)])
    #end setUpClass

    def _create_resource(self, res_type, proj_id, name=None, extra_res_fields=None):
        proj_id = proj_id.replace('-', '')
        context = {'operation': 'CREATE',
                   'user_id': '',
                   'is_admin': False,
                   'roles': '',
                   'tenant_id': proj_id}
        if name:
            res_name = name
        else:
            res_name = '%s-%s' %(res_type, str(uuid.uuid4()))
        data = {'resource': {'name': res_name,
                             'tenant_id': proj_id}}
        if extra_res_fields:
            data['resource'].update(extra_res_fields)

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return res_name, res_q
    # end _create_resource
    
    def _create_floatingip_and_associate_port_without_ext_gw(self, proj_id, name=None):
        #external network
        net_name, net_q = self._create_resource('network', proj_id, extra_res_fields={'router:external':True})
        subnet_name, subnet_q = self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': net_q['id'], 'cidr': '10.2.0.0/24', 'ip_version': 4})

        #private network
        pvt_net_name, pvt_net_q = self._create_resource('network', proj_id)
        pvt_subnet_name, pvt_subnet_q = self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': pvt_net_q['id'], 'cidr': '10.1.0.0/24', 'ip_version': 4})

        port_name, port_q = self._create_resource('port', proj_id, name, extra_res_fields={'network_id': pvt_subnet_q['network_id']})

        return self._create_resource('floatingip', proj_id, name, extra_res_fields={'floating_network_id': net_q['id'], 'port_id':port_q['id']})
    
    #test when strict_compliance is OFF
    def test_create_fip_and_associate_port_without_ext_gw(self):
        proj_obj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        for res_type in ['security_group']:
            res_name, res_q = getattr(self, '_create_' + res_type, lambda x:self._create_resource(res_type, x))(proj_obj.uuid)
            res_list = self._list_resources(res_type, proj_id=proj_obj.uuid, name=res_name)

        self._create_floatingip_and_associate_port_without_ext_gw(proj_obj.uuid)

    def _list_resources(self, res_type, fields=None, proj_id=None, name=None):
        proj_id = proj_id.replace('-', '')
        context = {'operation': 'READALL',
                   'userid': '',
                   'roles': '',
                   'is_admin': False,
                   'tenant': proj_id,
                   'tenant_id': proj_id}

        data = {'filters':{}, 'fields': None}
        if name:
            data.update({'filters': {'name': [name]}})
        if fields:
            data.update({'fields': fields})

        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return res_q
    # end _list_resources

# end class TestStrictCompOFF
