#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from pprint import pformat

from cfgm_common import _obj_serializer_all
from cfgm_common import jsonutils as json
from cfgm_common.exceptions import HttpError
from vnc_api.gen.resource_common import HostBasedService
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import ServiceVirtualNetworkType
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import VnSubnetsType

from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class HostBasedServiceServer(ResourceMixin, HostBasedService):
    LEFT = 'left'
    RIGHT = 'right'
    hbf = {
        LEFT: {
            'IP': '0.1.0.0',
            'PREFIX_LEN': '16',
            'NAME': 'hbf-left'
        },
        RIGHT: {
            'IP': '0.2.0.0',
            'PREFIX_LEN': '16',
            'NAME': 'hbf-right'
        }
    }

    @staticmethod
    def _check_only_one_vn_per_type(req_dict):
        vn_type_map = {}

        for ref in req_dict.get('virtual_network_refs', []):
            if 'attr' in ref and ref['attr'].get('virtual_network_type'):
                vn_type_map.setdefault(
                    ref['attr'].get('virtual_network_type'),
                    set([]),
                ).add(ref['uuid'])

        for vn_type, vn_uuids in list(vn_type_map.items()):
            if len(vn_uuids) > 1:
                msg = ("Virtual network type %s cannot be referenced by more "
                       "than one virtual network at a time" % vn_type)
                return False, (400, msg)

        return True, ''

    @staticmethod
    def is_default_vn_ref_valid(vn_type_set, vn_type, is_create):
        if is_create:
            # during create no vn_ref should be there.
            if vn_type in vn_type_set:
                return False
        else:
            # during update one vn_ref should be always there.
            if vn_type not in vn_type_set:
                return False

        return True

    @classmethod
    def _check_default_vn_valid(self, req_dict, is_create, non_vn_ref_update):
        if is_internal_request() or non_vn_ref_update:
            return True, ''
        vn_type_set = set()

        err_op = ''
        if is_create:
            err_op = 'created'
        else:
            err_op = 'updated'

        for ref in req_dict.get('virtual_network_refs', []):
            if 'attr' in ref and ref['attr'].get('virtual_network_type'):
                vn_type_set.add(ref['attr'].get('virtual_network_type'))

        vn_types = ['left', 'right']
        for vn_type in vn_types:
            if not self.is_default_vn_ref_valid(vn_type_set,
                                                vn_type, is_create):
                msg = ("default hbf-%s Virtual network  reference cannot be "
                       "%s" % (vn_type, err_op))
                return False, (400, msg)

        return True, ''

    @staticmethod
    def _check_type(req_dict, db_dict):
        if ('host_based_service_type' in req_dict and
                req_dict['host_based_service_type'] !=
                db_dict['host_based_service_type']):
            msg = "Cannot change the Host Based Service type"
            return False, (400, msg)

        return True, ''

    @classmethod
    def host_based_service_enabled(cls, project_uuid):
        ok, result = cls.server.get_resource_class(
            'project').locate(
            uuid=project_uuid, create_it=False,
            fields=['host_based_services'])
        if not ok:
            return False, result
        project = result

        hbss = project.get('host_based_services', [])
        if len(hbss) == 0:
            msg = ("Project %s(%s) have no host based service instantiated" %
                   (':'.join(project['fq_name']), project['uuid']))
            return True, (False, msg)

        if len(hbss) > 1:
            msg = ("Project %s(%s) have more than one host based service" %
                   (':'.join(project['fq_name']), project['uuid']))
            return True, (False, msg)

        ok, result = cls.server.get_resource_class(
            'host_based_service').locate(
            uuid=hbss[0]['uuid'], create_it=False,
            fields=['virtual_network_refs'])
        if not ok:
            return False, result
        hbs = result

        vn_types = set()
        for ref in hbs.get('virtual_network_refs', []):
            vn_types.add(ref['attr'].get('virtual_network_type'))
        if not set(['left', 'right']).issubset(vn_types):
            msg = ("Host based service %s(%s) needs at least one reference to "
                   "left and right virtual network to be considered as enable"
                   % (':'.join(hbs['fq_name']), hbs['uuid']))
            return True, (False, msg)

        return True, (True, '')

    @classmethod
    def _create_vn(cls, vn_fq_name, svn_type, hbs_uuid, parent_uuid):
        # Create a VN
        attrs = {
            'parent_type': 'project',
            'parent_uuid': parent_uuid,
        }
        ok, result = cls.server.get_resource_class(
            'virtual_network').locate(fq_name=vn_fq_name, **attrs)
        if not ok:
            return False, result
        vn_dict = result
        vn_uuid = vn_dict['uuid']

        attr = ServiceVirtualNetworkType(svn_type)
        attr_as_dict = attr.__dict__
        try:
            cls.server.internal_request_ref_update(
                'host-based-service', hbs_uuid, 'ADD',
                'virtual-network', vn_uuid,
                attr=attr_as_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)

        # Create a subnet and link it to default ipam
        # ref update to VN
        ipam_obj_type = 'network_ipam'
        ipam_fq_name = ['default-domain',
                        'default-project', 'default-network-ipam']
        ipam_uuid = cls.db_conn.fq_name_to_uuid(ipam_obj_type, ipam_fq_name)
        subnet = SubnetType(cls.hbf[svn_type]['IP'],
                            cls.hbf[svn_type]['PREFIX_LEN'])
        attr = VnSubnetsType([IpamSubnetType(subnet=subnet)])
        attr_as_dict = json.loads(json.dumps(
            attr, default=_obj_serializer_all))
        try:
            cls.server.internal_request_ref_update(
                'virtual-network', vn_uuid, 'ADD',
                'network-ipam', ipam_uuid,
                attr=attr_as_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)
        return ok, vn_dict

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        proj_name = obj_dict['fq_name'][:-1]
        # Create Left VN
        left = '__%s-%s__' % (obj_dict['fq_name'][-1],
                              cls.hbf[cls.LEFT]['NAME'])
        vn_left_fq_name = proj_name + [left]
        ok, resp = cls._create_vn(vn_left_fq_name, cls.LEFT,
                                  obj_dict['uuid'], obj_dict['parent_uuid'])
        if not ok:
            return ok, resp

        # Create Right VN
        right = '__%s-%s__' % (obj_dict['fq_name'][-1],
                               cls.hbf[cls.RIGHT]['NAME'])
        vn_right_fq_name = proj_name + [right]
        ok, resp = cls._create_vn(vn_right_fq_name, cls.RIGHT,
                                  obj_dict['uuid'], obj_dict['parent_uuid'])
        if not ok:
            return ok, resp

        return True, ''

    @classmethod
    def pre_dbe_create(cls, fq_name, obj_dict, db_conn):
        obj_dict.setdefault('host_based_service_type', 'firewall')

        ok, result = cls._check_default_vn_valid(obj_dict, True, False)
        if not ok:
            return ok, result

        return cls._check_only_one_vn_per_type(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, **kwargs):
        ok, result = cls.locate(uuid=id, create_it=False,
                                fields=['virtual_network_refs',
                                        'host_based_service_type'])
        if not ok:
            return False, result
        db_obj_dict = result

        non_vn_ref_update = False

        ok, result = cls._check_type(obj_dict, db_obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_only_one_vn_per_type(obj_dict)
        if not ok:
            return ok, result

        if 'virtual_network_refs' not in obj_dict:
            non_vn_ref_update = True

        ok, result = cls._check_default_vn_valid(
            obj_dict, False, non_vn_ref_update)
        if not ok:
            return ok, result

        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        for ref in obj_dict.get('virtual_network_refs', []):
            ok, result = cls.server.get_resource_class(
                'virtual_network').locate(
                uuid=ref['uuid'],
                fields=['virtual_machine_interface_back_refs', 'fq_name'],
                create_it=False)
            if not ok:
                return False, result, None
            if ok:
                if result.get('virtual_machine_interface_back_refs', []):
                    msg = ("HBS object (%s, %s), virtual network (%s,%s) "
                           "got interfaces, cannot be removed"
                           % (obj_dict['fq_name'],
                              id, ':'.join(result['fq_name']), ref['uuid']))
                    return False, (400, msg), None
        return True, "", None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        vn_types = ['left', 'right']
        for ref in obj_dict.get('virtual_network_refs', []):
            if ref['attr'].get('virtual_network_type') in vn_types:
                try:
                    cls.server.internal_request_delete('virtual-network',
                                                       ref['uuid'])
                except HttpError as e:
                    msg = ("Virtual network ref (%s, %s)"
                           "delete failed, status code %s, content (%s) "
                           % (ref['uuid'], ':'.join(ref['id']),
                              e.status_code, e.content))
                    return False, msg
        return True, ''

    # get namespace template
    @staticmethod
    def get_hbs_namespace(namespace):
        hbs_ns = {
            'apiVersion': 'v1',
            'kind': 'Namespace',
            'metadata': {'annotations': {'opencontrail.org/isolation': 'true'},
                         'name': '<namespace>'}
        }
        hbs_ns['metadata']['name'] = namespace
        return hbs_ns

    # end _get_hbs_namespace

    @staticmethod
    def _get_network(fqn):
        if isinstance(fqn, list) and len(fqn) == 3:
            return '{"domain":"%s", "project":"%s", "name":"%s"}' % (
                fqn[0], fqn[1], fqn[2])
        return ''

    # hbs network template
    @classmethod
    def get_hbs_network(self, vn_uuid, vn_k8s_str, namespace):
        hbs_nw = {
            'apiVersion': 'k8s.cni.cncf.io/v1',
            'kind': 'NetworkAttachmentDefinition',
            'metadata': {'annotations': {'opencontrail.org/network': ''},
                         'name': 'hbf-left',
                         'namespace': 'hp101'},
            'spec': {
                'config': '{"cniVersion":"0.3.0", "type": '
                          '"contrail-k8s-cni" }'}
        }
        vn_class = self.server.get_resource_class('virtual_network')
        ok, result = vn_class.locate(uuid=vn_uuid,
                                     fields=['network_ipam_refs'],
                                     create_it=False)
        if not ok:
            raise HttpError(result[0], result[1])
        vn_fq_name = result['fq_name']
        hbs_nw['metadata']['annotations'][
            'opencontrail.org/network'] = self._get_network(
            vn_fq_name)
        hbs_nw['metadata']['name'] = vn_k8s_str
        hbs_nw['metadata']['namespace'] = namespace
        return hbs_nw

    # end _get_hbs_network

    # Get hbs daemonset
    @classmethod
    def get_hbs_ds(self, hbs_info):
        hbs_ds_template = {
            'apiVersion': 'apps/v1',
            'kind': 'DaemonSet',
            'metadata': {'labels': {'type': 'hbf'},
                         'name': 'csrx',
                         'namespace': '<namespace>'},
            'spec': {'selector': {'matchLabels': {'name': 'hbf'}},
                     'template':
                         {'metadata': {
                             'annotations': {'k8s.v1.cni.cncf.io/networks':
                                             '[{"name":"left"},'
                                             ' {"name":"right"}]'},
                             'labels': {'name': 'hbf'}},
                             'spec': {'containers': [
                                 {'env': [{'name': 'CSRX_FORWARD_MODE',
                                           'value': 'wire'}],
                                  'image':
                                      'hub.juniper.net/security/csrx:19.2R1.8',
                                  'imagePullPolicy': 'IfNotPresent',
                                  'name': 'csrx',
                                  'securityContext': {'privileged': True},
                                  'stdin': True,
                                  'tty': False}],
                                 'imagePullSecrets': [{'name': 'psd'}],
                                 'nodeSelector': {'type': 'hbf'},
                                 'restartPolicy': 'Always'}}}
        }
        hbs_ds_template['metadata']['namespace'] = hbs_info['namespace']
        ds_template = hbs_ds_template['spec']['template']
        if hbs_info['vnmgmt']['fq_name']:
            ds_template['metadata']['annotations'][
                'opencontrail.org/networks'] = \
                self._get_network(hbs_info['vnmgmt']['fq_name'])
        ds_template_spec = ds_template['spec']
        if hbs_info['service']['image']:
            ds_template_spec['containers'][0]['image'] = hbs_info['service'][
                'image']
        if hbs_info['service']['imagePullSecrets']:
            ds_template_spec['imagePullSecrets'][0]['name'] = \
                hbs_info['service']['imagePullSecrets']
        return hbs_ds_template

    # end _get_hbs_ds

    # hbs object daemon set generation
    @classmethod
    def get_hbs_info(self, hbs_fq_name, hbs_uuid):
        hbs_info = {
            'cluster': None, 'namespace': None,
            'vnleft': {'fq_name': None, 'uuid': None, 'k8s_str': None},
            'vnright': {'fq_name': None, 'uuid': None, 'k8s_str': None},
            'vnmgmt': {'fq_name': None, 'uuid': None, 'k8s_str': None},
            'service': {'image': None, 'imagePullSecrets': None}
        }
        # get hbs object
        ok, result = self.locate(fq_name=hbs_fq_name,
                                 uuid=hbs_uuid,
                                 create_it=False)
        if not ok:
            raise HttpError(result[0], result[1])
        hbs_dict = result

        # get project and check for k8s, if not return an error
        project_uuid = hbs_dict['parent_uuid']
        project_class = self.server.get_resource_class('project')
        ok, result = project_class.locate(uuid=project_uuid,
                                          fields=['annotations'],
                                          create_it=False)
        if not ok:
            raise HttpError(result[0], result[1])
        project_dict = result
        project_fq_name = project_dict['fq_name']

        # Get namespace information from project annotations
        kvs = project_dict.get('annotations', {}).get('key_value_pair', [])
        namespace = None
        cluster = None
        for kv in kvs:
            if kv['key'] == 'namespace':
                namespace = kv['value']
            if kv['key'] == 'cluster':
                cluster = kv['value']
        if not namespace:
            raise HttpError(
                404, 'Project ' + pformat(project_fq_name) +
                     ' is not k8s project, namespace is not present')
        if not cluster:
            raise HttpError(
                404, 'Project ' + pformat(project_fq_name) +
                     ' is not k8s project, clsuter is not present')
        hbs_info['namespace'] = namespace
        hbs_info['cluster'] = cluster

        # Get vn refs, get both left and right VNs
        vn_refs = hbs_dict.get('virtual_network_refs', [])
        vn_left = vn_right = vn_mgmt = None
        for vn in vn_refs:
            attr = vn.get('attr', {})
            l_type = attr.get('virtual_network_type', None)
            if l_type == 'left':
                hbs_info['vnleft']['fq_name'] = vn['to']
                hbs_info['vnleft']['uuid'] = vn['uuid']
                vn_left = vn['to'][-1:][0]
                hbs_info['vnleft']['k8s_str'] = vn_left
            if l_type == 'right':
                hbs_info['vnright']['fq_name'] = vn['to']
                hbs_info['vnright']['uuid'] = vn['uuid']
                vn_right = vn['to'][-1:][0]
                hbs_info['vnright']['k8s_str'] = vn_right
            if l_type == 'management':
                hbs_info['vnmgmt']['fq_name'] = vn['to']
                hbs_info['vnmgmt']['uuid'] = vn['uuid']
                vn_mgmt = vn['to'][-1:][0]
                hbs_info['vnmgmt']['k8s_str'] = vn_mgmt
                # both left and right vn should be present
        if vn_left is None or vn_right is None:
            raise HttpError(
                404, 'Left and right HBF networks should be present')

        # get the map and look for image name and secrect
        kvs = hbs_dict.get('annotations', {}).get('key_value_pair', [])
        for kv in kvs:
            if kv['key'] == 'spec.template.spec.containers[].image' or \
                    kv['key'] == 'image':
                hbs_info['service']['image'] = kv['value']
            if kv['key'] == 'spec.template.spec.imagePullSecrets[].name' or \
                    kv['key'] == 'imagePullSecrets':
                hbs_info['service']['imagePullSecrets'] = kv['value']
        return hbs_info

    # end _get_hbs_info
