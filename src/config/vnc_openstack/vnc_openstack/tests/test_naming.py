import sys
import json
import uuid
import logging

sys.path.append('../common/tests')
from testtools.matchers import Equals, Contains, Not
from testtools import content, content_type

from vnc_api.vnc_api import *

from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)

class NBTestNaming(test_case.NeutronBackendTestCase):
    def _create_project(self, proj_name):
        proj_obj = Project(proj_name)
        self.addDetail('creating-project', content.text_content(proj_name))
        self._vnc_lib.project_create(proj_obj)
        default_sg_obj = SecurityGroup('default', parent_obj=proj_obj)
        self._vnc_lib.security_group_create(default_sg_obj)
        return proj_obj
    # end _create_project
        
    def _create_resource(self, res_type, proj_id, name=None, extra_res_fields=None):
        context = {'operation': 'CREATE',
                   'user_id': '',
                   'roles': ''}
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

    def _create_subnet(self, proj_id, name=None):
        net_name, net_q = self._create_resource('network', proj_id)
        return self._create_resource('subnet', proj_id, name, extra_res_fields={'network_id': net_q['id'], 'cidr': '1.0.0.0/24'})

    def _create_port(self, proj_id, name=None):
        #net_name, net_q = self._create_resource('network', proj_id)
        subnet_name, subnet_q = self._create_subnet(proj_id)
        return self._create_resource('port', proj_id, name, extra_res_fields={'network_id': subnet_q['network_id']})

    def _change_resource_name(self, res_type, res_q):
        new_res_name = 'new-%s-%s' %(res_type, str(uuid.uuid4()))
        context = {'operation': 'UPDATE',
                   'user_id': '',
                   'roles': ''}
        data = {'resource': {'name': new_res_name}, 
                'id': res_q['id']}
        body = {'context': context, 'data': data}
        resp = self._api_svr_app.post_json('/neutron/%s' %(res_type), body)
        res_q = json.loads(resp.text)
        return new_res_name, res_q
    # end _change_resource_name

    def _list_resources(self, res_type, fields=None, tenant_id=None, name=None):
        context = {'operation': 'READALL',
                   'userid': '',
                   'roles': '',
                   'is_admin': False,
                   'tenant': tenant_id}

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

    def test_name_change(self):
        proj_name = 'project-%s' %(str(uuid.uuid4()))
        proj_obj = self._create_project(proj_name)
        for res_type in ['network', 'subnet', 'security_group', 'port', 'router']:
            # create a resource
            res_name, res_q = getattr(self, '_create_' + res_type, lambda x:self._create_resource(res_type, x))(proj_obj.uuid)
            self.assertThat(res_q['name'], Equals(res_name))
            if res_type != 'subnet':
                self.assertThat(res_q['contrail:fq_name'], Contains(res_name))

            # change its name
            new_res_name, res_q = self._change_resource_name(res_type, res_q)
            self.assertThat(res_q['name'], Equals(new_res_name))
            if res_type != 'subnet':
                self.assertThat(res_q['contrail:fq_name'], Contains(res_name))
                self.assertThat(res_q['contrail:fq_name'], Not(Contains(new_res_name)))

            # list by filter of new name
            res_list = self._list_resources(res_type, tenant_id=proj_obj.uuid, name=new_res_name)
            self.assertThat(len(res_list), Equals(1))
            self.assertThat(res_list[0]['name'], Equals(new_res_name))

    # end test_name_change

    def test_duplicate_name(self):
        proj_name = 'project-%s' %(str(uuid.uuid4()))
        proj_obj = self._create_project(proj_name)
        for res_type in ['network', 'subnet', 'security_group', 'port', 'router']:
            # create a resource
            res_name, res_q = getattr(self, '_create_' + res_type, lambda x:self._create_resource(res_type, x))(proj_obj.uuid)
            self.assertThat(res_q['name'], Equals(res_name))

            # create another resource
            res_name, res_q = getattr(self, '_create_' + res_type, lambda x,name:self._create_resource(res_type, x, name))(proj_obj.uuid, res_name)
            self.assertThat(res_q['name'], Equals(res_name))
            if res_type != 'subnet':
                self.assertThat(res_q['contrail:fq_name'][-1], Not(Equals(res_name)))

            # list by filter of new name
            res_list = self._list_resources(res_type, tenant_id=proj_obj.uuid, name=res_name)
            self.assertThat(len(res_list), Equals(2))
# end class NBTestNaming

class KeystoneSync(test_case.KeystoneSyncTestCase):
    def test_dup_project(self):
        logger.info('Creating first project in "keystone"')
        proj_id = str(uuid.uuid4())
        proj_name = self.id()
        test_case.get_keystone_client().tenants.add_tenant(proj_id, proj_name)
        proj_obj = self._vnc_lib.project_read(id=proj_id)
        self.assertThat(proj_obj.name, Equals(proj_name))

        logger.info('Creating second project with same name diff id in "keystone"')
        new_proj_id = str(uuid.uuid4())
        test_case.get_keystone_client().tenants.add_tenant(new_proj_id, proj_name)
        new_proj_obj = self._vnc_lib.project_read(id=new_proj_id)
        self.assertThat(new_proj_obj.name, Not(Equals(proj_name)))
        self.assertThat(new_proj_obj.name, Contains(proj_name))

        self._vnc_lib.project_delete(id=proj_id)
        self._vnc_lib.project_delete(id=new_proj_id)
# end class KeystoneSync
