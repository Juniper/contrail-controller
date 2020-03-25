# Copyright 2012, Contrail Systems, Inc.
#

"""
.. attention:: Fix the license string
"""

from __future__ import absolute_import
from future import standard_library
standard_library.install_aliases()
from builtins import str
from past.builtins import basestring
from builtins import object
import sys
import copy
from collections import namedtuple
import requests
import re
import uuid
from cfgm_common import jsonutils as json
from cfgm_common import rest
from cfgm_common import PERMS_RWX, PERMS_NONE, PERMS_RX
from cfgm_common.utils import _DEFAULT_ZK_LOCK_PATH_PREFIX
from cfgm_common.utils import _DEFAULT_ZK_LOCK_TIMEOUT
import netaddr
from netaddr import IPNetwork, IPSet, IPAddress
import gevent
import bottle
import time
from kazoo.exceptions import LockTimeout

try:
    from neutron_lib import constants
except ImportError:
    from neutron.common import constants

from cfgm_common import exceptions as vnc_exc
from cfgm_common.utils import cgitb_hook
from cfgm_common import is_uuid_like
from vnc_api.vnc_api import *
from cfgm_common import SG_NO_RULE_FQ_NAME, UUID_PATTERN
import vnc_openstack
import vnc_cfg_api_server.context
from .context import get_context, use_context
import datetime
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import TagTypeIdToName
from six import StringIO

from vnc_openstack.utils import filter_fields
from vnc_openstack.utils import resource_is_in_use

operations = ['NOOP', 'CREATE', 'READ', 'UPDATE', 'DELETE']
oper = ['NOOP', 'POST', 'GET', 'PUT', 'DELETE']

_DEFAULT_HEADERS = {
    'Content-type': 'application/json; charset="UTF-8"', }

CREATE = 1
READ = 2
LIST = 3
UPDATE = 4
DELETE = 5

# SNAT defines
_IFACE_ROUTE_TABLE_NAME_PREFIX = 'NEUTRON_IFACE_RT'
_IFACE_ROUTE_TABLE_NAME_PREFIX_REGEX = re.compile(
    '%s_%s_%s' % (_IFACE_ROUTE_TABLE_NAME_PREFIX, UUID_PATTERN, UUID_PATTERN))
# FWaaS defines
_NEUTRON_FWAAS_TAG_TYPE = TagTypeIdToName[5]
_NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME = 'default'
_NEUTRON_FIREWALL_DEFAULT_IPV4_RULE_NAME = 'default ipv4'
_NEUTRON_FIREWALL_DEFAULT_IPV6_RULE_NAME = 'default ipv6'


class FakeVncLibResource(namedtuple('FakeVncLibResource', 'object_type uuid')):
    def get_uuid(self):
        return self.uuid

class LocalVncApi(VncApi):
    def __init__(self, api_server_obj, *args, **kwargs):
        if api_server_obj:
            self.api_server_routes = dict((r.rule.split('/')[1]+'-'+r.method, r.callback)
                for r in api_server_obj.api_bottle.routes)
            self.POST_FOR_LIST_THRESHOLD = sys.maxsize
        else:
            self.api_server_routes = []

        self.api_server_obj = api_server_obj
        super(LocalVncApi, self).__init__(*args, **kwargs)
    # def __init__

    def deepcopy_ref(self, ret_item, item_key):
        if not isinstance(ret_item, dict):
            return
        ref_backrefs = [f for f in ret_item[item_key]
                        if f.endswith('_refs')]
        for rb in ref_backrefs:
            # TODO optimize to deepcopy only if 'attr' is there
            ret_item[item_key][rb] = copy.deepcopy(
                ret_item[item_key][rb])

    @use_context
    def _request(self, op, url, data=None, *args, **kwargs):
        # Override vnc_lib._request so that list requests can reach
        # api_server_obj methods directly instead of system call.
        # Always pass contextual user_token aka mux connection to api-server
        orig_user_token = None
        started_time = time.time()
        req = get_context().request
        req_context = req.json.get('context', {}) if req.json else {}
        req_id = get_context().request_id
        user_token = get_context().user_token
        if 'X-AUTH-TOKEN' in self._headers:
            # retain/restore if there was already a token on channel
            orig_user_token = self._headers['X-AUTH-TOKEN']
            self._headers['X-AUTH-TOKEN'] = user_token

        url_parts = url.split('/')
        route = '%s-%s' % (url_parts[1], oper[op])
        if route not in self.api_server_routes:
            return super(LocalVncApi, self)._request(
                op, url, data, *args, **kwargs)

        server_method = self.api_server_routes[route]

        environ = {
            'PATH_INFO': url,
            'bottle.app': self.api_server_obj.api_bottle,
            'REQUEST_METHOD': oper[op],
            'HTTP_X_REQUEST_ID': req_id,
        }
        if user_token:
            auth_hdrs = vnc_cfg_api_server.auth_context.get_auth_hdrs()
        else:
            auth_hdrs = {}


        environ.update(auth_hdrs)

        if op == rest.OP_GET:
            if data:
                environ['QUERY_STRING'] = '&'.join(['%s=%s' % (k,v)
                                                    for k,v in data.items()])
        else:
            if data:
                x = StringIO()
                x.write(data)
                environ['bottle.request.data'] = x
                environ['bottle.request.json'] = json.loads(data)
                environ['CONTENT_TYPE'] = 'application/json; charset="UTF-8"',
                environ['CONTENT_LEN'] = x.tell()

        vnc_cfg_api_server.context.set_context(
            vnc_cfg_api_server.context.ApiContext(
                external_req=bottle.BaseRequest(environ))
                )

        try:
            status = 200
            ret_val = None
            ret_list = None

            if len(url_parts) < 3:
                ret_val = server_method()
            else:
                ret_val = server_method(url_parts[2])
            # make deepcopy of ref['attr'] as from_dict will update
            # in-place and change the cached value
            try:
                # strip / in /virtual-networks
                if ret_val:
                    if len(url_parts) < 3:
                        coll_key = url_parts[1]
                        item_key = coll_key[:-1]
                        ret_list = ret_val[coll_key]
                        for ret_item in ret_list:
                            self.deepcopy_ref(ret_item, item_key)
                    else:
                        item_key = url_parts[1]
                        ret_item = ret_val
                        self.deepcopy_ref(ret_item, item_key)
            except KeyError:
                pass

            if op == rest.OP_GET:
                return ret_val
            else:
                return json.dumps(ret_val)
        except bottle.HTTPError as http_error:
            status = http_error.status_code
            content = http_error.body

            if status == 404:
                raise vnc_exc.NoIdError(
                    'Error: oper %s url %s body %s response %s' %
                    (op, url, data, content)
                )
            elif status == 400:
                raise vnc_exc.BadRequest(status, content)
            elif status == 403:
                raise vnc_exc.PermissionDenied(content)
            elif status == 401:
                raise vnc_exc.AuthFailed(content)
            elif status == 409:
                raise vnc_exc.RefsExistError(content)
            elif status == 412:
                raise vnc_exc.OverQuota(content)
            else:
                raise vnc_exc.HttpError(status, content)
        finally:
            if orig_user_token:
                self._headers['X-AUTH-TOKEN'] = orig_user_token
            elapsed_time = time.time() - started_time
            trace = self.api_server_obj._generate_rest_api_request_trace()
            self.api_server_obj._generate_rest_api_response_trace(
                trace, ret_val)
            q = url
            if op == rest.OP_GET and environ.get('QUERY_STRING'):
                q = '%s?%s' % (q, environ.get('QUERY_STRING'))
            elif data:
                q = '%s %s' % (q, data)
            operation = req_context.get('operation', '-')
            self.api_server_obj.config_log(
                "VNC OpenStack: [%s %s %s] %s %s \"%s %s\" %d %d%s %0.6f" %
                (
                    req_id,
                    req_context.get('tenant_id', '-'),
                    req_context.get('user_id', '-'),
                    operation,
                    req_context.get('type', '-'),
                    oper[op],
                    q,
                    status if status else '-',
                    len(str(ret_val)) if ret_val else 0,
                    '(%d)' % len(ret_list) if (ret_list and
                        operation == 'READALL') else '',
                    elapsed_time,
                ), level=SandeshLevel.SYS_DEBUG)
    # end _request
# end class LocalVncApi


def catch_convert_exception(method):
    def wrapper(*args, **kwargs):
        if 'oper' in kwargs and kwargs['oper'] == LIST:
            try:
                return method(*args, **kwargs)
            except NoIdError:
                pass
            except Exception:
                cgitb_hook(format="text")
        else:
            return method(*args, **kwargs)

    return wrapper


class DBInterface(object):
    """
    An instance of this class forwards requests to vnc cfg api (web)server
    """
    Q_URL_PREFIX = '/extensions/ct'

    def __init__(self, manager, admin_name, admin_password, admin_tenant_name,
                 api_srvr_ip, api_srvr_port,
                 api_server_obj=None, user_info=None,
                 contrail_extensions_enabled=True,
                 list_optimization_enabled=False,
                 apply_subnet_host_routes=False,
                 strict_compliance=False):
        self._manager = manager
        self.logger = manager.logger
        self._api_srvr_ip = api_srvr_ip
        self._api_srvr_port = api_srvr_port
        self._api_server_obj = api_server_obj
        self._apply_subnet_host_routes = apply_subnet_host_routes
        self._strict_compliance = strict_compliance

        self._contrail_extensions_enabled = contrail_extensions_enabled
        self._list_optimization_enabled = list_optimization_enabled

        # Set the lock prefix for security_group_rule modifications
        self.lock_path_prefix = '%s/%s' % (self._api_server_obj._args.cluster_id,
                                           _DEFAULT_ZK_LOCK_PATH_PREFIX)
        self.security_group_lock_prefix = '%s/security_group' % self.lock_path_prefix
        self.virtual_network_lock_prefix = '%s/virtual_network' % self.lock_path_prefix
        self._zookeeper_client = self._api_server_obj._db_conn._zk_db._zk_client

        # Retry till a api-server is up
        self._connected_to_api_server = gevent.event.Event()
        connected = False
        while not connected:
            try:
                self._vnc_lib = LocalVncApi(self._api_server_obj,
                                       admin_name, admin_password,
                                       admin_tenant_name, api_srvr_ip,
                                       api_srvr_port, '/', user_info=user_info,
                                       exclude_hrefs=True)
                self._connected_to_api_server.set()
                connected = True
            except requests.exceptions.RequestException as e:
                gevent.sleep(3)

    #end __init__

    # Helper routines
    def _get_project_obj(self, data_q):
        project_id = str(uuid.UUID(data_q['tenant_id']))
        project_obj = self._project_read(proj_id=project_id)
        return project_obj
    # end _get_project_obj

    def _get_resource_id(self, data_q, create_id=False):
        resource_id = data_q.get('id', None)
        if resource_id == None and create_id:
            resource_id = str(uuid.uuid4())
        return resource_id
    # end _get_resource_id

    def _request_api_server(self, url, method, data=None, headers=None):
        from eventlet.greenthread import getcurrent
        token = getcurrent().contrail_vars.token

        if method == 'GET':
            return requests.get(url)
        if method == 'POST':
            return requests.post(url, data=data, headers=headers)
        if method == 'DELETE':
            return requests.delete(url)
    #end _request_api_server

    def _relay_request(self, request):
        """
        Send received request to api server
        """
        # chop neutron parts of url and add api server address
        url_path = re.sub(self.Q_URL_PREFIX, '', request.environ['PATH_INFO'])
        url = "http://%s:%s%s" % (self._api_srvr_ip, self._api_srvr_port,
                                  url_path)

        return self._request_api_server(
            url, request.environ['REQUEST_METHOD'],
            request.body, {'Content-type': request.environ['CONTENT_TYPE']})
    #end _relay_request

    def _validate_project_ids(self, context=None, filters=None):
        if context and not context['is_admin']:
            return [str(uuid.UUID(context['tenant']))]

        return_project_ids = []
        for project_id in (filters.get('tenant_id', []) +
                           filters.get('project_id', [])):
            try:
                return_project_ids.append(str(uuid.UUID(project_id)))
            except ValueError:
                continue

        return return_project_ids or None

    def _obj_to_dict(self, obj):
        return self._vnc_lib.obj_to_dict(obj)
    #end _obj_to_dict

    def _get_plugin_property(self, property_in):
        fq_name=['default-global-system-config'];
        gsc_obj = self._vnc_lib.global_system_config_read(fq_name);
        plugin_settings = gsc_obj.plugin_tuning.plugin_property
        for each_setting in plugin_settings:
            if each_setting.property == property_in:
                return each_setting.value
        return None
    #end _get_plugin_property

    def _ensure_instance_exists(self, instance_id, tenant_id, baremetal=False):
        instance_name = instance_id

        instance_obj = VirtualMachine(instance_name)
        try:
            try:
                uuid.UUID(instance_id)
                instance_obj.uuid = instance_id
            except ValueError:
                # if instance_id is not a valid uuid, let
                # virtual_machine_create generate uuid for the vm
                pass
            # set instance ownership to real tenant
            perms2 = PermType2()
            perms2.owner = tenant_id
            instance_obj.set_perms2(perms2)

            # Specify the Server(VM) type
            if baremetal:
                instance_obj.set_server_type('baremetal-server')
            else:
                instance_obj.set_server_type('virtual-server')

            # create object
            self._vnc_lib.virtual_machine_create(instance_obj)
        except RefsExistError as e:
            if instance_obj.uuid:
                db_instance_obj = self._vnc_lib.virtual_machine_read(
                    id=instance_obj.uuid)
            else:
                db_instance_obj = self._vnc_lib.virtual_machine_read(
                    fq_name=instance_obj.fq_name)
            # In case of baremetal, multiple ports will exist on BMS.
            # so, the object may already exisit. In this case just update it
            if baremetal and db_instance_obj.get_server_type() != "baremetal-server":
                self._vnc_lib.virtual_machine_update(instance_obj)
            else:
                instance_obj = db_instance_obj
        except AuthFailed as e:
            self._raise_contrail_exception('NotAuthorized', msg=str(e))

        return instance_obj
    #end _ensure_instance_exists

    def _ensure_default_security_group_exists(self, proj_id):
        proj_id = str(uuid.UUID(proj_id))
        proj_obj = self._vnc_lib.project_read(id=proj_id, fields=['security_groups'])
        vnc_openstack.ensure_default_security_group(self._vnc_lib, proj_obj)
    #end _ensure_default_security_group_exists

    def _get_obj_tenant_id(self, q_type, obj_uuid):
        # Seed the cache and return
        if q_type == 'port':
            port_obj = self._virtual_machine_interface_read(obj_uuid)
            if port_obj.parent_type != "project":
                net_id = port_obj.get_virtual_network_refs()[0]['uuid']
                # recurse up type-hierarchy
                tenant_id = self._get_obj_tenant_id('network', net_id)
            else:
                tenant_id = port_obj.parent_uuid.replace('-', '')
            return tenant_id

        if q_type == 'network':
            net_obj = self._virtual_network_read(net_id=obj_uuid)
            tenant_id = net_obj.parent_uuid.replace('-', '')
            return tenant_id

        return None
    #end _get_obj_tenant_id

    def _project_read(self, proj_id=None, fq_name=None):
        proj_obj = self._vnc_lib.project_read(id=proj_id, fq_name=fq_name)
        return proj_obj
    #end _project_read

    def _get_tenant_id_for_create(self, context, resource):
        if context['is_admin'] and 'tenant_id' in resource:
            tenant_id = resource['tenant_id']
        elif ('tenant_id' in resource and
              resource['tenant_id'] != context['tenant_id']):
            reason = 'Cannot create resource for another tenant'
            self._raise_contrail_exception('AdminRequired', reason=reason)
        else:
            tenant_id = context['tenant_id']
        return tenant_id

    # Encode and send an exception information to neutron. exc must be a
    # valid exception class name in neutron, kwargs must contain all
    # necessary arguments to create that exception
    @staticmethod
    def _raise_contrail_exception(exc, **kwargs):
        exc_info = {'exception': exc}
        exc_info.update(kwargs)
        bottle.abort(400, json.dumps(exc_info))
    #end _raise_contrail_exception

    def _security_group_rule_create(self, sg_id, sg_rule):
        # Before we can update the rule inside security_group, get the
        # lock first. This lock will be released in finally block below.
        scope_lock = self._zookeeper_client.lock(
            '%s/%s' % (
                self.security_group_lock_prefix, sg_id
            ))

        # (SATHISH) This is a temp fix for fixing lost update problem during
        # Parallel creation of Security Group Rule
        try:
            acquired_lock = scope_lock.acquire(timeout=_DEFAULT_ZK_LOCK_TIMEOUT)

            # If this node acquired the lock, continue with creation of
            # security_group rule.
            try:
                sg_vnc = self._vnc_lib.security_group_read(id=sg_id)
            except NoIdError:
                scope_lock.release()
                self._raise_contrail_exception('SecurityGroupNotFound', id=sg_id)

            rules = sg_vnc.get_security_group_entries()
            if rules is None:
                rules = PolicyEntriesType([sg_rule])
            else:
                rules.add_policy_rule(sg_rule)
            sg_vnc.set_security_group_entries(rules)

            try:
                self._resource_update('security_group', sg_vnc)
            except BadRequest as e:
                scope_lock.release()
                self._raise_contrail_exception('BadRequest',
                    resource='security_group_rule', msg=str(e))
            except OverQuota as e:
                scope_lock.release()
                self._raise_contrail_exception('OverQuota',
                    overs=['security_group_rule'], msg=str(e))
            except RefsExistError as e:
                try:
                    rule_uuid = str(e).split(':')[1].strip()
                except IndexError:
                    rule_uuid = None
                scope_lock.release()
                self._raise_contrail_exception('SecurityGroupRuleExists',
                    resource='security_group_rule', id=rule_uuid, rule_id=rule_uuid)

        except LockTimeout:
            # If the lock was not acquired and timeout of 5 seconds happened, then raise
            # a bad request error.
            msg = ("Security Group Rule could not be created, Try again.. ")
            self._raise_contrail_exception('BadRequest',
                resource='security_group_rule', msg=msg)
        finally:
            scope_lock.release()

    # end _security_group_rule_create

    def _security_group_rule_find(self, sgr_id, project_uuid=None):
        # Get all security group for a project if project uuid is specified
        # else get all security groups in the system(admin context)
        project_sgs = self._security_group_list_project(project_uuid)

        for sg_obj in project_sgs:
            sgr_entries = sg_obj.get_security_group_entries()
            if sgr_entries is None:
                continue

            for sg_rule in sgr_entries.get_policy_rule():
                if sg_rule.get_rule_uuid() == sgr_id:
                    return sg_obj, sg_rule

        return None, None
    #end _security_group_rule_find

    def _security_group_rule_delete(self, sg_obj, sg_rule):
        # Before we can delete the rule inside security_group, get the
        # lock first. This lock will be released in finally block below.
        scope_lock = self._zookeeper_client.lock(
            '%s/%s' % (
                self.security_group_lock_prefix, sg_obj.uuid
            ))

        try:
            acquired_lock = scope_lock.acquire(timeout=_DEFAULT_ZK_LOCK_TIMEOUT)
            # If this node acquired the lock, continue with deletion of
            # security_group rule.
            rules = sg_obj.get_security_group_entries()
            rules.get_policy_rule().remove(sg_rule)
            sg_obj.set_security_group_entries(rules)
            self._resource_update('security_group', sg_obj)
        except LockTimeout:
            # If the lock was not acquired and timeout of 5 seconds happened, then raise
            # a bad request error.
            msg = ("Security Group Rule could not be deleted, Try again.. ")
            self._raise_contrail_exception('BadRequest',
                resource='security_group_rule', msg=msg)
        finally:
            scope_lock.release()
    #end _security_group_rule_delete

    def _security_group_delete(self, sg_id):
        self._resource_delete('security_group', sg_id)
    #end _security_group_delete

    def _svc_instance_create(self, si_obj):
        try:
            si_uuid = self._resource_create('service_instance', si_obj)
        except RefsExistError as e:
            self._raise_contrail_exception('BadRequest',
                resource='svc_instance', msg=str(e))
        st_fq_name = ['default-domain', 'nat-template']
        st_obj = self._vnc_lib.service_template_read(fq_name=st_fq_name)
        si_obj.set_service_template(st_obj)
        self._resource_update('service_instance', si_obj)

        return si_uuid
    #end _svc_instance_create

    def _svc_instance_delete(self, si_id):
        self._resource_delete('service_instance', si_id)
    #end _svc_instance_delete

    def _route_table_create(self, rt_obj):
        rt_uuid = self._resource_create('route_table', rt_obj)
        return rt_uuid
    #end _route_table_create

    def _route_table_delete(self, rt_id):
        self._resource_delete('route_table', rt_id)
    #end _route_table_delete

    def _resource_create(self, resource_type, obj):
        create_method = getattr(self._vnc_lib, resource_type + '_create')
        try:
            try:
                obj_uuid = create_method(obj)
            except RefsExistError:
                orig_obj_name = obj.name
                if not obj.uuid:
                    obj.uuid = str(uuid.uuid4())
                try:
                    # try to change only name and fq_name before changing uuid
                    obj.name += '-' + obj.uuid
                    obj.fq_name[-1] += '-' + obj.uuid
                    obj_uuid = create_method(obj)
                except RefsExistError:
                    obj.uuid = str(uuid.uuid4())
                    obj.name = orig_obj_name + '-' + obj.uuid
                    obj.fq_name[-1] = orig_obj_name + '-' + obj.uuid
                    obj_uuid = create_method(obj)
        except BadRequest as e:
            self._raise_contrail_exception('BadRequest',
                resource=resource_type, msg=str(e))
        except OverQuota as e:
            self._raise_contrail_exception('OverQuota',
                overs=[resource_type], msg=str(e))
        except AuthFailed as e:
            self._raise_contrail_exception('NotAuthorized', msg=str(e))
        return obj_uuid
    #end _resource_create

    def _resource_update(self, resource_type, obj):
        update_method = getattr(self._vnc_lib, resource_type + '_update')
        try:
            update_method(obj)
        except AuthFailed as e:
            self._raise_contrail_exception('NotAuthorized', msg=str(e))
    #end _resource_update

    def _resource_delete(self, resource_type, obj_id):
        delete_method = getattr(self._vnc_lib, resource_type + '_delete')
        try:
            delete_method(id=obj_id)
        except AuthFailed as e:
            self._raise_contrail_exception('NotAuthorized', msg=str(e))
    #end _resource_delete

    def _resource_read_by_tag(self, tags, fields=None):
        filters = []
        for tag in tags:
            filters.append('neutron_tag={}'.format(tag))
        url = '/tags?fq_names={}'.format(','.join(filters))
        if fields:
            url += '&fields={}'.format(fields)
        return self._vnc_lib._request(rest.OP_GET, url)
    #end _resource_read_by_tag

    def _virtual_network_read(self, net_id=None, fq_name=None, fields=None):
        net_obj = self._vnc_lib.virtual_network_read(id=net_id,
                                                     fq_name=fq_name,
                                                     fields=fields)
        return net_obj
    #end _virtual_network_read

    def _virtual_network_update(self, net_obj):
        try:
            self._resource_update('virtual_network', net_obj)
        except RefsExistError as e:
            self._raise_contrail_exception('BadRequest',
                resource='network', msg=str(e))
    # end _virtual_network_update

    def _virtual_network_list(self, parent_id=None, obj_uuids=None,
                              fields=None, detail=False, count=False,
                              filters=None):
        return self._vnc_lib.virtual_networks_list(
                                              parent_id=parent_id,
                                              obj_uuids=obj_uuids,
                                              fields=fields,
                                              detail=detail,
                                              count=count,
                                              filters=filters,
                                              shared=True)
    #end _virtual_network_list

    def _virtual_machine_interface_read(self, port_id=None, fq_name=None,
                                        fields=None):
        back_ref_fields = ['logical_router_back_refs', 'instance_ip_back_refs',
                           'floating_ip_back_refs']
        prop_ref_fields = list(VirtualMachineInterface.prop_fields |
                               VirtualMachineInterface.ref_fields)
        if fields:
            n_extra_fields = list(set(fields + back_ref_fields))
        else:
            n_extra_fields = list(set(prop_ref_fields + back_ref_fields))

        port_obj = self._vnc_lib.virtual_machine_interface_read(
            id=port_id, fq_name=fq_name, fields=n_extra_fields)
        return port_obj
    #end _virtual_machine_interface_read

    def _virtual_machine_interface_update(self, port_obj):
        self._resource_update('virtual_machine_interface', port_obj)
    #end _virtual_machine_interface_update

    def _virtual_machine_interface_delete(self, port_id):
        self._resource_delete('virtual_machine_interface', port_id)
    #end _virtual_machine_interface_delete

    def _virtual_machine_interface_list(self, parent_id=None, back_ref_id=None,
                                        obj_uuids=None, fields=None,
                                        filters=None):
        back_ref_fields = ['logical_router_back_refs', 'instance_ip_back_refs',
                           'floating_ip_back_refs']
        if fields:
            n_extra_fields = list(set(fields + back_ref_fields))
        else:
            n_extra_fields = back_ref_fields
        vmi_objs = self._vnc_lib.virtual_machine_interfaces_list(
                                                     parent_id=parent_id,
                                                     back_ref_id=back_ref_id,
                                                     obj_uuids=obj_uuids,
                                                     detail=True,
                                                     fields=n_extra_fields,
                                                     filters=filters)
        return vmi_objs
    #end _virtual_machine_interface_list

    def _instance_ip_create(self, iip_obj):
        try:
            iip_uuid = self._vnc_lib.instance_ip_create(iip_obj)
        except AuthFailed as e:
            self._raise_contrail_exception('NotAuthorized', msg=str(e))

        return iip_uuid
    #end _instance_ip_create

    def _instance_ip_read(self, instance_ip_id=None, fq_name=None):
        iip_obj = self._vnc_lib.instance_ip_read(id=instance_ip_id,
                                                 fq_name=fq_name)
        return iip_obj
    #end _instance_ip_read

    def _instance_ip_update(self, iip_obj):
        self._resource_update('instance_ip', iip_obj)
    #end _instance_ip_update

    def _instance_ip_delete(self, instance_ip_id):
        self._resource_delete('instance_ip', instance_ip_id)
    #end _instance_ip_delete

    def _virtual_machine_list(self, back_ref_id=None, obj_uuids=None, fields=None):
        vm_objs = self._vnc_lib.virtual_machines_list(detail=True,
                                                   back_ref_id=back_ref_id,
                                                   obj_uuids=obj_uuids,
                                                   fields=fields)
        return vm_objs
    #end _virtual_machine_list

    def _instance_ip_list(self, back_ref_id=None, obj_uuids=None, fields=None):
        iip_objs = self._vnc_lib.instance_ips_list(detail=True,
                                                   back_ref_id=back_ref_id,
                                                   obj_uuids=obj_uuids,
                                                   fields=fields)
        return iip_objs
    # end _instance_ip_list

    def _floating_ip_pool_create(self, net_obj):
        fip_pool_obj = FloatingIpPool('floating-ip-pool', net_obj)
        # fip pool should be shared
        fip_pool_obj.perms2 = PermType2(
            net_obj.parent_uuid, PERMS_RWX,    # tenant, tenant-access
            PERMS_RWX,                   # global-access
            [])                          # share list
        fip_pool_uuid = self._resource_create('floating_ip_pool', fip_pool_obj)

        return fip_pool_uuid
    # end _floating_ip_pool_create

    def _floating_ip_pool_delete(self, fip_pool):
        if 'floating_ips' in fip_pool:
            fip_list = fip_pool.get('floating_ips')
            fip_ids = [fip_id['uuid'] for fip_id in fip_list]
            fip_dict = self._vnc_lib.floating_ips_list(obj_uuids=fip_ids,
                        fields=['virtual_machine_interface_refs'],
                        detail=False)
            fips = fip_dict.get('floating-ips')

            #Delete fip if it's not associated with any port
            for fip in fips:
                if 'virtual_machine_interface_refs' in fip:
                    raise RefsExistError
            for fip in fips:
                self.floatingip_delete(fip_id=fip['uuid'])

        fip_pool_id = fip_pool['uuid']
        fip_pool_uuid = self._resource_delete('floating_ip_pool', fip_pool_id)
    # end _floating_ip_pool_delete

    # find projects on a given domain
    def _project_list_domain(self, domain_id):
        # TODO till domain concept is not present in keystone
        fq_name = ['default-domain']
        resp_dict = self._vnc_lib.projects_list(parent_fq_name=fq_name)

        return resp_dict['projects']
    #end _project_list_domain

    # find network ids on a given project
    def _network_list_project(self, project_id, count=False, filters=None):
        if project_id is not None:
            try:
                project_id = str(uuid.UUID(project_id))
            except (TypeError, ValueError, AttributeError):
                project_id = None

        if count:
            ret_val = self._virtual_network_list(parent_id=project_id,
                                                 count=True, filters=filters)
        else:
            ret_val = self._virtual_network_list(parent_id=project_id,
                                                 detail=True, filters=filters)

        return ret_val
    #end _network_list_project

    # find router ids on a given project
    def _router_list_project(self, project_id=None, detail=False):
        if project_id:
            try:
                project_uuid = str(uuid.UUID(project_id))
            except (TypeError, ValueError, AttributeError):
                return []
        else:
            project_uuid = None

        resp = self._vnc_lib.logical_routers_list(parent_id=project_uuid,
                                                  detail=detail)
        if detail:
            return resp

        return resp['logical-routers']
    #end _router_list_project

    def _ipam_list_project(self, project_id):
        try:
            project_uuid = str(uuid.UUID(project_id))
        except (TypeError, ValueError, AttributeError):
            return []

        resp_dict = self._vnc_lib.network_ipams_list(parent_id=project_uuid)

        return resp_dict['network-ipams']
    #end _ipam_list_project

    def _security_group_list_project(self, project_id, filters=None):
        if project_id:
            try:
                project_uuid = str(uuid.UUID(project_id))
                # Trigger a project read to ensure project sync
                project_obj = self._project_read(proj_id=project_uuid)
            except vnc_exc.NoIdError:
                return []
        else:
            project_uuid = None

        obj_uuids = set()
        if filters and 'id' in filters:
            obj_uuids.update(filters['id'])
        if filters and 'security_group_id' in filters:
            obj_uuids.update(filters['security_group_id'])
        sg_objs = self._vnc_lib.security_groups_list(
            parent_id=project_uuid,
            detail=True,
            obj_uuids=list(obj_uuids) or None)
        return sg_objs
    #end _security_group_list_project

    def _security_group_entries_list_sg(self, sg_id):
        try:
            sg_uuid = str(uuid.UUID(sg_id))
        except (TypeError, ValueError, AttributeError):
            return []

        resp_dict = self._vnc_lib.security_groups_list(obj_uuids=[sg_uuid])

        return resp_dict['security-groups']
    #end _security_group_entries_list_sg

    def _route_table_list_project(self, project_id):
        try:
            project_uuid = str(uuid.UUID(project_id))
        except (TypeError, ValueError, AttributeError):
            return []

        resp_dict = self._vnc_lib.route_tables_list(parent_id=project_uuid)

        return resp_dict['route-tables']
    #end _route_table_list_project

    def _svc_instance_list_project(self, project_id):
        try:
            project_uuid = str(uuid.UUID(project_id))
        except (TypeError, ValueError, AttributeError):
            return []

        resp_dict = self._vnc_lib.service_instances_list(parent_id=project_id)

        return resp_dict['service-instances']
    #end _svc_instance_list_project

    def _policy_list_project(self, project_id):
        try:
            project_uuid = str(uuid.UUID(project_id))
        except (TypeError, ValueError, AttributeError):
            return []

        resp_dict = self._vnc_lib.network_policys_list(parent_id=project_uuid)

        return resp_dict['network-policys']
    #end _policy_list_project

    def _logical_router_list(self, parent_id=None, back_ref_id=None,
                             obj_uuids=None, fields=None):
        rtr_obj = self._vnc_lib.logical_routers_list(parent_id=parent_id,
                                                     back_ref_id=back_ref_id,
                                                     obj_uuids=obj_uuids,
                                                     detail=True,
                                                     fields=fields)
        return rtr_obj
    #end _logical_router_list

    def _logical_router_read(self, rtr_id=None, fq_name=None):
        rtr_obj = self._vnc_lib.logical_router_read(id=rtr_id, fq_name=fq_name)
        return rtr_obj
    #end _logical_router_read

    def _logical_router_update(self, rtr_obj):
        self._resource_update('logical_router', rtr_obj)
    # end _logical_router_update

    def _logical_router_delete(self, rtr_id):
        try:
            self._resource_delete('logical_router', rtr_id)
        except RefsExistError:
            self._raise_contrail_exception('RouterInUse', router_id=rtr_id)
    #end _logical_router_delete

    # find floating ip pools a project has access to
    def _fip_pool_refs_project(self, project_id):
        project_obj = self._project_read(proj_id=project_id)

        return project_obj.get_floating_ip_pool_refs()
    #end _fip_pool_refs_project

    def _network_list_filter(self, shared=None, router_external=None):
        filters = {}
        if shared is not None:
            filters['is_shared'] = shared
        if router_external is not None:
            filters['router_external'] = router_external

        net_list = self._network_list_project(project_id=None, filters=filters)
        return net_list
    # end _network_list_filter

    # find networks of floating ip pools project has access to
    def _fip_pool_ref_networks(self, project_id):
        ret_net_objs = self._network_list_filter(shared=True)

        proj_fip_pool_refs = self._fip_pool_refs_project(project_id)
        if not proj_fip_pool_refs:
            return ret_net_objs

        for fip_pool_ref in proj_fip_pool_refs:
            fip_uuid = fip_pool_ref['uuid']
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(id=fip_uuid)
            net_uuid = fip_pool_obj.parent_uuid
            net_obj = self._virtual_network_read(net_id=net_uuid)
            ret_net_objs.append(net_obj)

        return ret_net_objs
    #end _fip_pool_ref_networks

    # find floating ip pools defined by network
    def _fip_pool_list_network(self, net_id, fields=None):
        resp_dict = self._vnc_lib.floating_ip_pools_list(parent_id=net_id, fields=fields)

        return resp_dict['floating-ip-pools']
    #end _fip_pool_list_network

    def _port_list(self, port_objs):
        ret_q_ports = []

        if not port_objs:
            return ret_q_ports

        memo_req = {'networks': {},
                    'subnets': {},
                    'virtual-machines': {},
                    'instance-ips': {},
                    'service-instances': {}}

        # Read only the nets associated to port_objs
        net_refs = [port_obj.get_virtual_network_refs() for port_obj in port_objs]
        net_ids = set(ref[0]['uuid'] for ref in net_refs if ref)
        net_objs = self._virtual_network_list(obj_uuids=list(net_ids),
                                              detail=True)
        for net_obj in net_objs:
            # dictionary of iip_uuid to iip_obj
            memo_req['networks'][net_obj.uuid] = net_obj
            subnets_info = self._virtual_network_to_subnets(net_obj)
            memo_req['subnets'][net_obj.uuid] = subnets_info

        # Read only the instance-ips associated to port_objs
        iip_objs = self._instance_ip_list(back_ref_id=
                                  [port_obj.uuid for port_obj in port_objs])
        memo_req['instance-ips'] = dict((iip_obj.uuid, iip_obj) for iip_obj in iip_objs)

        # Read only the VMs associated to port_objs
        vm_ids = []
        for port_obj in port_objs:
            if port_obj.parent_type == 'virtual-machine':
                # created in <1.06 schema with VM as port parent
                vm_id = self._vnc_lib.fq_name_to_id('virtual-machine',
                                             port_obj.get_fq_name()[:-1])
                vm_ids.append(vm_id)
            else:
                vm_refs = port_obj.get_virtual_machine_refs() or []
                vm_ids.extend([ref['uuid'] for ref in vm_refs if ref])

        vm_objs = self._virtual_machine_list(obj_uuids=vm_ids)
        memo_req['virtual-machines'] = dict((vm_obj.uuid, vm_obj) for vm_obj in vm_objs)

        # Read only SIs associated with vm_objs
        si_ids = [si_ref['uuid']
                    for vm_obj in vm_objs
                    for si_ref in vm_obj.get_service_instance_refs() or []]
        if si_ids:
            si_objs = self._vnc_lib.service_instances_list(
                obj_uuids=si_ids, fields=['logical_router_back_refs'],
                detail=True)
            memo_req['service-instances'] = dict(
                (si_obj.uuid, si_obj) for si_obj in si_objs)

        # Convert port from contrail to neutron repr with the memo cache
        for port_obj in port_objs:
            port_info = self._port_vnc_to_neutron(port_obj, memo_req, oper=LIST)
            if port_info is None:
                continue
            ret_q_ports.append(port_info)

        return ret_q_ports
    #end _port_list

    def _port_list_network(self, network_ids, count=False):
        ret_list = []
        if not network_ids:
            return ret_list

        all_port_objs = self._virtual_machine_interface_list(
                                       back_ref_id=network_ids)

        return self._port_list(all_port_objs)
    #end _port_list_network

    # find port ids on a given project
    def _port_list_project(self, project_id, count=False, is_admin=False):
        if self._list_optimization_enabled:
            if count:
                port_objs = self._virtual_machine_interface_list(parent_id=project_id)
                return len(port_objs)

            # it is a list operation, not count
            all_port_objs = self._virtual_machine_interface_list(parent_id=project_id)
            return self._port_list(all_port_objs)
        else:
            if count:
                ret_val = 0
            else:
                ret_val = []
            net_objs = self._virtual_network_list(project_id,
                            fields=['virtual_machine_interface_back_refs'],
                            detail=True)
            if not net_objs:
                return ret_val

            if count:
                for net_obj in net_objs:
                    port_back_refs = (
                        net_obj.get_virtual_machine_interface_back_refs() or [])
                    ret_val = ret_val + len(port_back_refs)
                return ret_val

            net_ids = [net_obj.uuid for net_obj in net_objs]
            port_objs = self._virtual_machine_interface_list(back_ref_id=net_ids)
            return self._port_list(port_objs)
    #end _port_list_project

    # Returns True if
    #     * no filter is specified
    #     OR
    #     * search-param is not present in filters
    #     OR
    #     * 1. search-param is present in filters AND
    #       2. resource matches param-list AND
    #       3. shared parameter in filters is False
    def _filters_is_present(self, filters, key_name, match_value):
        if filters:
            if key_name in filters:
                try:
                    if key_name == 'tenant_id':
                        filter_value = [str(uuid.UUID(t_id))
                                        for t_id in filters[key_name]]
                    else:
                        filter_value = filters[key_name]
                    idx = filter_value.index(match_value)
                except ValueError:  # not in requested list
                    return False
        return True
    #end _filters_is_present

    def _network_read(self, net_uuid):
        net_obj = self._virtual_network_read(net_id=net_uuid)
        return net_obj
    # end _network_read

    def _subnet_vnc_read_mapping(self, id):
        try:
            return self._vnc_lib.kv_retrieve(id)
        except NoIdError:
            return None
    # end _subnet_vnc_read_mapping

    def _subnet_vnc_get_prefix(self, subnet_vnc):
        if subnet_vnc.subnet:
            pfx = subnet_vnc.subnet.get_ip_prefix()
            pfx_len = subnet_vnc.subnet.get_ip_prefix_len()
        else:
            pfx = '0.0.0.0'
            pfx_len = 0

        network = IPNetwork('%s/%s' % (pfx, pfx_len))
        return str(network)
    # end _subnet_vnc_get_prefix

    def _subnet_read(self, net_uuid, prefix):
        try:
            net_obj = self._virtual_network_read(net_id=net_uuid)
        except NoIdError:
            return None

        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs or []:
            subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
            for subnet_vnc in subnet_vncs:
                if self._subnet_vnc_get_prefix(subnet_vnc) == prefix:
                    return subnet_vnc

        return None
    # end _subnet_read

    # Returns a list of dicts of subnet-id:cidr for a VN
    def _virtual_network_to_subnets(self, net_obj):
        ret_subnets = []

        ipam_refs = net_obj.get_network_ipam_refs()
        if ipam_refs:
            for ipam_ref in ipam_refs:
                subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
                for subnet_vnc in subnet_vncs:
                    subnet_id = subnet_vnc.subnet_uuid
                    cidr = self._subnet_vnc_get_prefix(subnet_vnc)
                    ret_subnets.append({'id': subnet_id, 'cidr': cidr})

        return ret_subnets
    # end _virtual_network_to_subnets

    # Conversion routines between VNC and Quantum objects
    def _svc_instance_neutron_to_vnc(self, si_q, oper):
        if oper == CREATE:
            project_obj = self._get_project_obj(si_q)
            net_id = si_q['external_net']
            ext_vn = self._vnc_lib.virtual_network_read(id=net_id)
            scale_out = ServiceScaleOutType(max_instances=1, auto_scale=False)
            si_prop = ServiceInstanceType(
                      auto_policy=True,
                      left_virtual_network="",
                      right_virtual_network=ext_vn.get_fq_name_str(),
                      scale_out=scale_out)
            si_prop.set_scale_out(scale_out)
            si_vnc = ServiceInstance(name=si_q['name'],
                         parent_obj=project_obj,
                         service_instance_properties=si_prop)
        return si_vnc
    #end _svc_instance_neutron_to_vnc

    @catch_convert_exception
    def _svc_instance_vnc_to_neutron(self, si_obj, oper=READ):
        si_q_dict = self._obj_to_dict(si_obj)

        # replace field names
        si_q_dict['id'] = si_obj.uuid
        si_q_dict['tenant_id'] = si_obj.parent_uuid.replace('-', '')
        si_q_dict['name'] = si_obj.name
        si_props = si_obj.get_service_instance_properties()
        if si_props:
            vn_fq_name = si_props.get_right_virtual_network()
            vn_obj = self._vnc_lib.virtual_network_read(fq_name_str=vn_fq_name)
            si_q_dict['external_net'] = str(vn_obj.uuid) + ' ' + vn_obj.name
            si_q_dict['internal_net'] = ''
        return si_q_dict
    #end _svc_instance_vnc_to_neutron

    def _route_table_neutron_to_vnc(self, rt_q, oper):
        if oper == CREATE:
            project_obj = self._get_project_obj(rt_q)
            rt_vnc = RouteTable(name=rt_q['name'],
                                parent_obj=project_obj)

            if not rt_q['routes']:
                return rt_vnc
            for route in rt_q['routes']['route']:
                try:
                    vm_obj = self._vnc_lib.virtual_machine_read(
                        id=route['next_hop'])
                    si_list = vm_obj.get_service_instance_refs()
                    if si_list:
                        fq_name = si_list[0]['to']
                        si_obj = self._vnc_lib.service_instance_read(
                            fq_name=fq_name)
                        route['next_hop'] = si_obj.get_fq_name_str()
                except Exception as e:
                    pass
            rt_vnc.set_routes(RouteTableType.factory(**rt_q['routes']))
        else:
            rt_vnc = self._vnc_lib.route_table_read(id=rt_q['id'])

            for route in rt_q['routes']['route']:
                try:
                    vm_obj = self._vnc_lib.virtual_machine_read(
                        id=route['next_hop'])
                    si_list = vm_obj.get_service_instance_refs()
                    if si_list:
                        fq_name = si_list[0]['to']
                        si_obj = self._vnc_lib.service_instance_read(
                            fq_name=fq_name)
                        route['next_hop'] = si_obj.get_fq_name_str()
                except Exception as e:
                    pass
            rt_vnc.set_routes(RouteTableType.factory(**rt_q['routes']))

        return rt_vnc
    #end _route_table_neutron_to_vnc

    @catch_convert_exception
    def _route_table_vnc_to_neutron(self, rt_obj, oper=READ):
        rt_q_dict = self._obj_to_dict(rt_obj)

        # replace field names
        rt_q_dict['id'] = rt_obj.uuid
        rt_q_dict['tenant_id'] = rt_obj.parent_uuid.replace('-', '')
        rt_q_dict['name'] = rt_obj.name
        rt_q_dict['fq_name'] = rt_obj.fq_name

        # get route table routes
        rt_q_dict['routes'] = rt_q_dict.pop('routes', None)
        if rt_q_dict['routes']:
            for route in rt_q_dict['routes']['route']:
                if route['next_hop_type']:
                    route['next_hop'] = route['next_hop_type']
        return rt_q_dict
    # end _route_table_vnc_to_neutron

    @catch_convert_exception
    def _security_group_vnc_to_neutron(self, sg_obj, memo_req=None, oper=READ):
        sg_q_dict = {}
        extra_dict = {}
        extra_dict['fq_name'] = sg_obj.get_fq_name()

        # replace field names
        sg_q_dict['id'] = sg_obj.uuid
        sg_q_dict['tenant_id'] = sg_obj.parent_uuid.replace('-', '')
        if not sg_obj.display_name:
            # for security groups created directly via vnc_api
            sg_q_dict['name'] = sg_obj.get_fq_name()[-1]
        else:
            sg_q_dict['name'] = sg_obj.display_name

        # If description is not specified by the user, assign empty string
        sg_q_dict['description'] = sg_obj.get_id_perms().get_description()
        if not sg_q_dict['description']:
            sg_q_dict['description'] = ""

        # get security group rules
        sg_q_dict['security_group_rules'] = []
        rule_list = self.security_group_rules_read(sg_obj.uuid, sg_obj,
                                                   memo_req)
        for rule in rule_list or []:
            sg_q_dict['security_group_rules'].append(rule)

        sg_q_dict['created_at'] = sg_obj.get_id_perms().get_created()
        sg_q_dict['updated_at'] = sg_obj.get_id_perms().get_last_modified()

        if self._contrail_extensions_enabled:
            sg_q_dict.update(extra_dict)
        return sg_q_dict
    # end _security_group_vnc_to_neutron

    def _security_group_neutron_to_vnc(self, sg_q, oper):
        if oper == CREATE:
            project_obj = self._get_project_obj(sg_q)

            id_perms = IdPermsType(enable=True,
                                   description=sg_q.get('description'))
            sg_vnc = SecurityGroup(name=sg_q['name'],
                                   parent_obj=project_obj,
                                   id_perms=id_perms)
            sg_id = self._get_resource_id(sg_q, False)
            if sg_id is not None:
                sg_vnc.set_uuid(sg_id)
        else:
            sg_vnc = self._vnc_lib.security_group_read(id=sg_q['id'])

        if oper == UPDATE and sg_vnc.name == 'default':
            self._raise_contrail_exception("SecurityGroupCannotUpdateDefault")

        if 'name' in sg_q and sg_q['name']:
            sg_vnc.display_name = sg_q['name']
        if 'description' in sg_q:
            id_perms = sg_vnc.get_id_perms()
            id_perms.set_description(sg_q['description'])
            sg_vnc.set_id_perms(id_perms)
        return sg_vnc
    # end _security_group_neutron_to_vnc

    @catch_convert_exception
    def _security_group_rule_vnc_to_neutron(self, sg_id, sg_rule, sg_obj=None,
                                            memo_req=None, oper=READ):
        sgr_q_dict = {}
        if sg_id is None:
            return sgr_q_dict

        if not sg_obj:
            try:
                sg_obj = self._vnc_lib.security_group_read(id=sg_id)
            except NoIdError:
                self._raise_contrail_exception('SecurityGroupNotFound',
                                               id=sg_id)

        remote_cidr = None
        remote_sg_uuid = None
        saddr = sg_rule.get_src_addresses()[0]
        daddr = sg_rule.get_dst_addresses()[0]
        if saddr.get_security_group() == 'local':
            direction = 'egress'
            addr = daddr
        elif daddr.get_security_group() == 'local':
            direction = 'ingress'
            addr = saddr
        else:
            self._raise_contrail_exception('SecurityGroupRuleNotFound',
                                           id=sg_rule.get_rule_uuid())

        remote_subnet = addr.get_subnet()
        remote_sg = addr.get_security_group()
        if remote_subnet:
            remote_cidr = '%s/%s' % (remote_subnet.get_ip_prefix(),
                                     remote_subnet.get_ip_prefix_len())
        elif remote_sg and remote_sg not in ['any', 'local']:
            if remote_sg != sg_obj.get_fq_name_str():
                remote_sg_uuid = None
                if memo_req and memo_req.get('security_groups'):
                    remote_sg_uuid = memo_req['security_groups'].get(remote_sg)
                if not remote_sg_uuid:
                    try:
                        remote_sg_uuid = self._vnc_lib.fq_name_to_id(
                            'security-group', remote_sg.split(':'))
                    except NoIdError:
                        # Filter rule out as the remote security group does not
                        # exist anymore
                        return sgr_q_dict
            else:
                remote_sg_uuid = sg_obj.uuid

        sgr_q_dict['id'] = sg_rule.get_rule_uuid()
        sgr_q_dict['tenant_id'] = sg_obj.parent_uuid.replace('-', '')
        sgr_q_dict['security_group_id'] = sg_obj.uuid
        sgr_q_dict['ethertype'] = sg_rule.get_ethertype()
        sgr_q_dict['direction'] = direction
        sgr_q_dict['protocol'] = sg_rule.get_protocol()

        if sg_rule.get_dst_ports():
            sgr_q_dict['port_range_min'] = sg_rule.get_dst_ports()[0].\
                get_start_port()
            sgr_q_dict['port_range_max'] = sg_rule.get_dst_ports()[0].\
                get_end_port()
        else:
            sgr_q_dict['port_range_min'] = 0
            sgr_q_dict['port_range_max'] = 65535

        sgr_q_dict['remote_ip_prefix'] = remote_cidr
        sgr_q_dict['remote_group_id'] = remote_sg_uuid

        sgr_q_dict['created_at'] = sg_rule.get_created()
        sgr_q_dict['updated_at'] = sg_rule.get_last_modified()

        return sgr_q_dict
    # end _security_group_rule_vnc_to_neutron

    def _security_group_rule_neutron_to_vnc(self, sgr_q, oper):
        if oper == CREATE:
            # default port values
            if sgr_q['protocol'] in (constants.PROTO_NAME_ICMP,
                                     str(constants.PROTO_NUM_ICMP)):
                port_min = None
                port_max = None
            else:
                port_min = 0
                port_max = 65535

            if sgr_q['port_range_min'] is not None:
                port_min = sgr_q['port_range_min']
            if sgr_q['port_range_max'] is not None:
                port_max = sgr_q['port_range_max']

            if sgr_q['remote_ip_prefix'] and sgr_q['remote_group_id']:
                self._raise_contrail_exception(
                    'SecurityGroupRemoteGroupAndRemoteIpPrefix')
            endpt = [AddressType(security_group='any')]
            if sgr_q['remote_ip_prefix']:
                et = sgr_q.get('ethertype')
                ip_net = netaddr.IPNetwork(sgr_q['remote_ip_prefix'])
                if ((ip_net.version == 4 and et != 'IPv4') or
                        (ip_net.version == 6 and et != 'IPv6')):
                    self._raise_contrail_exception(
                        'SecurityGroupRuleParameterConflict',
                        ethertype=et, cidr=sgr_q['remote_ip_prefix'])
                cidr = sgr_q['remote_ip_prefix'].split('/')
                pfx = cidr[0]
                pfx_len = int(cidr[1])
                endpt = [AddressType(subnet=SubnetType(pfx, pfx_len))]
            elif sgr_q['remote_group_id']:
                try:
                    sg_obj = self._vnc_lib.security_group_read(
                        id=sgr_q['remote_group_id'])
                except NoIdError:
                    self._raise_contrail_exception('SecurityGroupNotFound',
                                                   id=sgr_q['remote_group_id'])
                endpt = [AddressType(security_group=sg_obj.get_fq_name_str())]

            if sgr_q['direction'] == 'ingress':
                local = endpt
                remote = [AddressType(security_group='local')]
            else:
                remote = endpt
                local = [AddressType(security_group='local')]

            if not sgr_q['protocol']:
                sgr_q['protocol'] = 'any'

            invalid = False
            protos = ['any',
                      constants.PROTO_NAME_TCP,
                      constants.PROTO_NAME_UDP,
                      constants.PROTO_NAME_ICMP]
            if sgr_q['protocol'].isdigit():
                protocol = int(sgr_q['protocol'])
                if protocol < 0 or protocol > 255:
                    invalid = True
            else:
                if sgr_q['protocol'] not in protos:
                    invalid = True

            if invalid:
                self._raise_contrail_exception(
                    'SecurityGroupRuleInvalidProtocol',
                    protocol=sgr_q['protocol'],
                    values=protos)

            if not sgr_q['remote_ip_prefix'] and not sgr_q['remote_group_id']:
                if not sgr_q['ethertype']:
                    sgr_q['ethertype'] = 'IPv4'

            sgr_uuid = self._get_resource_id(sgr_q, True)

            # Added timestamp for tempest test case
            timestamp_at_create = datetime.datetime.utcnow().isoformat()

            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                  protocol=sgr_q['protocol'],
                                  src_addresses=local,
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=remote,
                                  dst_ports=[PortType(port_min, port_max)],
                                  ethertype=sgr_q['ethertype'],
                                  created=timestamp_at_create,
                                  last_modified=timestamp_at_create)
            return rule
    # end _security_group_rule_neutron_to_vnc

    def _network_neutron_to_vnc(self, network_q, oper):
        attr_not_specified = object()
        net_name = network_q.get('name', None)
        try:
            external_attr = network_q['router:external']
        except KeyError:
            external_attr = attr_not_specified
        if oper == CREATE:
            project_obj = self._get_project_obj(network_q)
            id_perms = IdPermsType(enable=True)
            net_obj = VirtualNetwork(net_name, project_obj, id_perms=id_perms)
            if external_attr == attr_not_specified:
                net_obj.router_external = False
            else:
                net_obj.router_external = external_attr
                # external network should be readable and reference-able from
                # outside
                if external_attr:
                    net_obj.perms2 = PermType2(
                        project_obj.uuid, PERMS_RWX, # tenant, tenant-access
                        PERMS_RX,                    # global-access
                        [])                          # share list
            if 'shared' in network_q:
                net_obj.is_shared = network_q['shared']
            else:
                net_obj.is_shared = False
            net_id = self._get_resource_id(network_q, False)
            if net_id:
                net_obj.uuid = net_id
        else:  # READ/UPDATE/DELETE
            net_obj = self._virtual_network_read(net_id=network_q['id'])
            if oper == UPDATE:
                if 'shared' in network_q:
                    net_obj.is_shared = network_q['shared']
                if external_attr is not attr_not_specified:
                    net_obj.router_external = external_attr

        if 'name' in network_q and network_q['name']:
            net_obj.display_name = network_q['name']

        phys_net = network_q.get('provider:physical_network')
        seg_id = network_q.get('provider:segmentation_id')

        if seg_id or phys_net:
            net_obj.set_provider_properties(ProviderDetails(seg_id, phys_net))

        id_perms = net_obj.get_id_perms()
        if 'admin_state_up' in network_q:
            id_perms.enable = network_q['admin_state_up']
            net_obj.set_id_perms(id_perms)

        if 'policys' in network_q:
            policy_fq_names = network_q['policys']
            # reset and add with newly specified list
            net_obj.set_network_policy_list([], [])
            seq = 0
            for p_fq_name in policy_fq_names:
                domain_name, project_name, policy_name = p_fq_name

                domain_obj = Domain(domain_name)
                project_obj = Project(project_name, domain_obj)
                policy_obj = NetworkPolicy(policy_name, project_obj)

                net_obj.add_network_policy(policy_obj,
                                           VirtualNetworkPolicyType(
                                           sequence=SequenceType(seq, 0)))
                seq = seq + 1

        if 'route_table' in network_q:
            rt_fq_name = network_q['route_table']
            if rt_fq_name:
                try:
                    rt_obj = self._vnc_lib.route_table_read(fq_name=rt_fq_name)
                    net_obj.set_route_table(rt_obj)
                except NoIdError:
                    # TODO add route table specific exception
                    self._raise_contrail_exception('NetworkNotFound',
                                                   net_id=net_obj.uuid)
        if oper == CREATE or oper == UPDATE:
            if 'port_security_enabled' in network_q:
                net_obj.set_port_security_enabled(network_q['port_security_enabled'])

        if 'description' in network_q:
            id_perms = net_obj.get_id_perms()
            id_perms.set_description(network_q['description'])
            net_obj.set_id_perms(id_perms)

        return net_obj
    #end _network_neutron_to_vnc

    def _shared_with_tenant(self, context, share_list):
        if context and context.get('tenant') and len(share_list):
            _uuid = str(uuid.UUID(context.get('tenant')))
            for share in share_list:
                if _uuid == share.tenant:
                    return True
        return False
    #end _shared_with_tenant

    def _is_shared(self, resource):
        perms2 = resource.get_perms2()
        if perms2:
            if (perms2.get_global_access() == PERMS_RWX or
                    len(perms2.get_share())):
                return True
        return False

    @catch_convert_exception
    def _network_vnc_to_neutron(self, net_obj, oper=READ, context=None):
        net_q_dict = {}
        extra_dict = {}

        id_perms = net_obj.get_id_perms()
        perms = id_perms.permissions
        net_q_dict['id'] = net_obj.uuid

        if not net_obj.display_name:
            # for nets created directly via vnc_api
            net_q_dict['name'] = net_obj.get_fq_name()[-1]
        else:
            net_q_dict['name'] = net_obj.display_name

        extra_dict['fq_name'] = net_obj.get_fq_name()
        net_q_dict['tenant_id'] = net_obj.parent_uuid.replace('-', '')
        net_q_dict['project_id'] = net_obj.parent_uuid.replace('-', '')
        net_q_dict['admin_state_up'] = id_perms.enable
        if net_obj.is_shared or (net_obj.perms2 and
                self._shared_with_tenant(context, net_obj.perms2.share)):
            net_q_dict['shared'] = True
        else:
            net_q_dict['shared'] = False
        net_q_dict['status'] = (constants.NET_STATUS_ACTIVE if id_perms.enable
                                else constants.NET_STATUS_DOWN)
        if net_obj.router_external:
            net_q_dict['router:external'] = True
        else:
            net_q_dict['router:external'] = False

        if net_obj.provider_properties:
            net_q_dict['provider:physical_network'] = net_obj.provider_properties.physical_network
            net_q_dict['provider:segmentation_id'] = net_obj.provider_properties.segmentation_id

        if oper == READ or oper == LIST:
            net_policy_refs = net_obj.get_network_policy_refs()
            if net_policy_refs:
                sorted_refs = sorted(
                    net_policy_refs,
                    key=lambda t:(t['attr'].sequence.major,
                                  t['attr'].sequence.minor))
                extra_dict['policys'] = [np_ref['to']
                                                  for np_ref in sorted_refs]

        rt_refs = net_obj.get_route_table_refs()
        if rt_refs:
            extra_dict['route_table'] = [rt_ref['to']
                                                  for rt_ref in rt_refs]

        ipam_refs = net_obj.get_network_ipam_refs()
        net_q_dict['subnets'] = []
        if ipam_refs:
            extra_dict['subnet_ipam'] = []
            for ipam_ref in ipam_refs:
                subnets = ipam_ref['attr'].get_ipam_subnets()
                for subnet in subnets:
                    sn_dict = self._subnet_vnc_to_neutron(subnet, net_obj,
                                                          ipam_ref['to'])
                    if sn_dict is None:
                        continue
                    net_q_dict['subnets'].append(sn_dict['id'])
                    sn_ipam = {}
                    sn_ipam['subnet_cidr'] = sn_dict['cidr']
                    sn_ipam['ipam_fq_name'] = ipam_ref['to']
                    extra_dict['subnet_ipam'].append(sn_ipam)

        if self._contrail_extensions_enabled:
            net_q_dict.update(extra_dict)
        net_q_dict['port_security_enabled'] = net_obj.get_port_security_enabled()

        if net_obj.get_id_perms().get_description() is not None:
            net_q_dict['description'] = net_obj.get_id_perms().get_description()

        net_q_dict['created_at'] = net_obj.get_id_perms().get_created()
        net_q_dict['updated_at'] = net_obj.get_id_perms().get_last_modified()

        return net_q_dict
    #end _network_vnc_to_neutron

    def _subnet_neutron_to_vnc(self, subnet_q):
        cidr = IPNetwork(subnet_q['cidr'])
        pfx = str(cidr.network)
        pfx_len = int(cidr.prefixlen)
        if cidr.version != 4 and cidr.version != 6:
            self._raise_contrail_exception('BadRequest',
                resource='subnet', msg='Unknown IP family')
        elif cidr.version != int(subnet_q['ip_version']):
            msg = "cidr '%s' does not match the ip_version '%s'" \
                    %(subnet_q['cidr'], subnet_q['ip_version'])
            self._raise_contrail_exception('InvalidInput', error_message=msg)
        if 'gateway_ip' in subnet_q:
            default_gw = subnet_q['gateway_ip']
        else:
            # Assigned first+1 from cidr
            default_gw = str(IPAddress(cidr.first + 1))

        if 'allocation_pools' in subnet_q:
            alloc_pools = subnet_q['allocation_pools']
        else:
            # Assigned by address manager
            alloc_pools = None

        dhcp_option_list = None
        dns_server_address = None
        if 'dns_nameservers' in subnet_q and subnet_q['dns_nameservers']:
            dhcp_options=[]
            dns_servers=" ".join(subnet_q['dns_nameservers'])
            if dns_servers:
                dhcp_options.append(DhcpOptionType(dhcp_option_name='6',
                                                   dhcp_option_value=dns_servers))
                dns_server_address = subnet_q['dns_nameservers'][0]
            if dhcp_options:
                dhcp_option_list = DhcpOptionsListType(dhcp_options)

        host_route_list = None
        if 'host_routes' in subnet_q and subnet_q['host_routes']:
            host_routes=[]
            for host_route in subnet_q['host_routes']:
                host_routes.append(RouteType(prefix=host_route['destination'],
                                             next_hop=host_route['nexthop']))
            if host_routes:
                host_route_list = RouteTableType(host_routes)

        if 'enable_dhcp' in subnet_q:
            dhcp_config = subnet_q['enable_dhcp']
        else:
            dhcp_config = None
        sn_name=subnet_q.get('name')
        # Added timestamp for tempest test case
        timestamp_at_create = datetime.datetime.utcnow().isoformat()
        subnet_id = self._get_resource_id(subnet_q, True)

        dns_s_addr='0.0.0.0' if self._strict_compliance else dns_server_address
        subnet_vnc = IpamSubnetType(subnet=SubnetType(pfx, pfx_len),
                                    default_gateway=default_gw,
                                    dns_server_address=dns_s_addr,
                                    enable_dhcp=dhcp_config,
                                    dns_nameservers=None,
                                    allocation_pools=alloc_pools,
                                    addr_from_start=True,
                                    dhcp_option_list=dhcp_option_list,
                                    host_routes=host_route_list,
                                    subnet_name=sn_name,
                                    subnet_uuid=subnet_id,
                                    created=timestamp_at_create,
                                    last_modified=timestamp_at_create)

        return subnet_vnc
    #end _subnet_neutron_to_vnc

    @catch_convert_exception
    def _subnet_vnc_to_neutron(self, subnet_vnc, net_obj, ipam_fq_name, oper=READ):
        sn_q_dict = {}
        extra_dict = {}
        sn_name = subnet_vnc.get_subnet_name()
        if sn_name is not None:
            sn_q_dict['name'] = sn_name
        else:
            sn_q_dict['name'] = ''
        sn_q_dict['tenant_id'] = net_obj.parent_uuid.replace('-', '')
        sn_q_dict['network_id'] = net_obj.uuid
        sn_q_dict['ipv6_ra_mode'] = None
        sn_q_dict['ipv6_address_mode'] = None
        if subnet_vnc.subnet:
            cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                              subnet_vnc.subnet.get_ip_prefix_len())
            sn_q_dict['cidr'] = cidr
            sn_q_dict['ip_version'] = IPNetwork(cidr).version # 4 or 6
        else:
            sn_q_dict['cidr'] = '0.0.0.0/0'
            sn_q_dict['ip_version'] = 4

        sn_id = subnet_vnc.subnet_uuid
        sn_q_dict['id'] = sn_id

        sn_q_dict['gateway_ip'] = subnet_vnc.default_gateway
        if sn_q_dict['gateway_ip'] == '0.0.0.0':
            sn_q_dict['gateway_ip'] = None

        alloc_obj_list = subnet_vnc.get_allocation_pools()
        allocation_pools = []
        for alloc_obj in alloc_obj_list:
            first_ip = alloc_obj.get_start()
            last_ip = alloc_obj.get_end()
            alloc_dict = {'first_ip':first_ip, 'last_ip':last_ip}
            allocation_pools.append(alloc_dict)

        if subnet_vnc.subnet:
            if allocation_pools is None or not allocation_pools:
                if (sn_q_dict['gateway_ip'] is not None and
                   (int(IPNetwork(sn_q_dict['gateway_ip']).network) ==
                    int(IPNetwork(cidr).network+1))):
                    first_ip = str(IPNetwork(cidr).network + 2)
                else:
                    first_ip = str(IPNetwork(cidr).network + 1)
                last_ip = str(IPNetwork(cidr).broadcast - 1)
                cidr_pool = {'first_ip':first_ip, 'last_ip':last_ip}
                allocation_pools.append(cidr_pool)
        else:
            cidr_pool = {'first_ip':'0.0.0.0', 'last_ip':'255.255.255.255'}
            allocation_pools.append(cidr_pool)
        sn_q_dict['allocation_pools'] = allocation_pools

        sn_q_dict['enable_dhcp'] = subnet_vnc.get_enable_dhcp()

        nameserver_dict_list = list()
        dhcp_option_list = subnet_vnc.get_dhcp_option_list()
        if dhcp_option_list:
            for dhcp_option in dhcp_option_list.dhcp_option:
                if dhcp_option.get_dhcp_option_name() == '6':
                    dns_servers = dhcp_option.get_dhcp_option_value().split()
                    for dns_server in dns_servers:
                        nameserver_entry = {'address': dns_server,
                                            'subnet_id': sn_id}
                        nameserver_dict_list.append(nameserver_entry)
        sn_q_dict['dns_nameservers'] = nameserver_dict_list
        # dhcp_option_list could have other options.
        # Check explicitly if dns_nameservers are
        # provided in the dhcp_options by the user.
        # If it is NOT, then report the dns server
        # allocated by contrail.
        if not nameserver_dict_list:
           extra_dict['dns_server_address'] = subnet_vnc.dns_server_address
        if self._contrail_extensions_enabled:
           sn_q_dict.update(extra_dict)

        host_route_dict_list = list()
        host_routes = subnet_vnc.get_host_routes()
        if host_routes:
            for host_route in host_routes.route:
                host_route_entry = {'destination': host_route.get_prefix(),
                                    'nexthop': host_route.get_next_hop(),
                                    'subnet_id': sn_id}
                host_route_dict_list.append(host_route_entry)
        sn_q_dict['routes'] = host_route_dict_list

        if net_obj.is_shared or (net_obj.perms2 and len(net_obj.perms2.share)):
            sn_q_dict['shared'] = True
        else:
            sn_q_dict['shared'] = False

        sn_q_dict['created_at'] = subnet_vnc.get_created()
        sn_q_dict['updated_at'] = subnet_vnc.get_last_modified()

        return sn_q_dict
    #end _subnet_vnc_to_neutron

    def _ipam_neutron_to_vnc(self, ipam_q, oper):
        ipam_name = ipam_q.get('name', None)
        if oper == CREATE:
            project_obj = self._get_project_obj(ipam_q)
            ipam_obj = NetworkIpam(ipam_name, project_obj)
        else:  # READ/UPDATE/DELETE
            ipam_obj = self._vnc_lib.network_ipam_read(id=ipam_q['id'])

        options_vnc = DhcpOptionsListType()
        if ipam_q['mgmt']:
            #for opt_q in ipam_q['mgmt'].get('options', []):
            #    options_vnc.add_dhcp_option(DhcpOptionType(opt_q['option'],
            #                                               opt_q['value']))
            #ipam_mgmt_vnc = IpamType.factory(
            #                    ipam_method = ipam_q['mgmt']['method'],
            #                                 dhcp_option_list = options_vnc)
            ipam_obj.set_network_ipam_mgmt(IpamType.factory(**ipam_q['mgmt']))

        return ipam_obj
    #end _ipam_neutron_to_vnc

    @catch_convert_exception
    def _ipam_vnc_to_neutron(self, ipam_obj, oper=READ):
        ipam_q_dict = self._obj_to_dict(ipam_obj)

        # replace field names
        ipam_q_dict['id'] = ipam_q_dict.pop('uuid')
        ipam_q_dict['name'] = ipam_obj.name
        ipam_q_dict['tenant_id'] = ipam_obj.parent_uuid.replace('-', '')
        ipam_q_dict['mgmt'] = ipam_q_dict.pop('network_ipam_mgmt', None)
        net_back_refs = ipam_q_dict.pop('virtual_network_back_refs', None)
        if net_back_refs:
            ipam_q_dict['nets_using'] = []
            for net_back_ref in net_back_refs:
                net_fq_name = net_back_ref['to']
                ipam_q_dict['nets_using'].append(net_fq_name)

        return ipam_q_dict
    #end _ipam_vnc_to_neutron

    def _policy_neutron_to_vnc(self, policy_q, oper):
        policy_name = policy_q.get('name', None)
        if oper == CREATE:
            project_obj = self._get_project_obj(policy_q)
            policy_obj = NetworkPolicy(policy_name, project_obj)
        else:  # READ/UPDATE/DELETE
            policy_obj = self._vnc_lib.network_policy_read(id=policy_q['id'])

        policy_rule = policy_q.get('entries')
        if policy_rule:
            if isinstance(policy_rule, dict):
                policy_obj.set_network_policy_entries(
                    PolicyEntriesType.factory(**policy_q['entries']))
            else:
                msg = 'entries must be a dict'
                self._raise_contrail_exception('BadRequest',
                                               resource="policy", msg=msg)
        policy_obj.set_network_policy_entries(
            PolicyEntriesType.factory(**policy_q['entries']))

        return policy_obj
    #end _policy_neutron_to_vnc

    @catch_convert_exception
    def _policy_vnc_to_neutron(self, policy_obj, oper=READ):
        policy_q_dict = self._obj_to_dict(policy_obj)

        # replace field names
        policy_q_dict['id'] = policy_q_dict.pop('uuid')
        policy_q_dict['name'] = policy_obj.name
        policy_q_dict['tenant_id'] = policy_obj.parent_uuid.replace('-', '')
        policy_q_dict['entries'] = policy_q_dict.pop('network_policy_entries',
                                                     None)
        net_back_refs = policy_obj.get_virtual_network_back_refs()
        if net_back_refs:
            policy_q_dict['nets_using'] = []
            for net_back_ref in net_back_refs:
                net_fq_name = net_back_ref['to']
                policy_q_dict['nets_using'].append(net_fq_name)

        return policy_q_dict
    #end _policy_vnc_to_neutron

    def _router_neutron_to_vnc(self, router_q, oper):
        rtr_name = router_q.get('name', None)
        if oper == CREATE:
            project_obj = self._get_project_obj(router_q)
            id_perms = IdPermsType(enable=True)
            rtr_obj = LogicalRouter(rtr_name, project_obj, id_perms=id_perms)
            router_id = self._get_resource_id(router_q, False)
            if router_id:
                rtr_obj.uuid = router_id
        else:  # READ/UPDATE/DELETE
            rtr_obj = self._logical_router_read(rtr_id=router_q['id'])

        id_perms = rtr_obj.get_id_perms()
        if 'admin_state_up' in router_q:
            id_perms.enable = router_q['admin_state_up']
            rtr_obj.set_id_perms(id_perms)

        if 'name' in router_q and router_q['name']:
            rtr_obj.display_name = router_q['name']

        if 'description' in router_q:
            id_perms = rtr_obj.get_id_perms()
            id_perms.set_description(router_q['description'])
            rtr_obj.set_id_perms(id_perms)

        return rtr_obj
    #end _router_neutron_to_vnc

    def _get_external_gateway_info(self, rtr_obj):
        vn_refs = rtr_obj.get_virtual_network_refs()
        if not vn_refs:
            return None

        return vn_refs[0]['uuid']

    @catch_convert_exception
    def _router_vnc_to_neutron(self, rtr_obj, oper=READ):
        rtr_q_dict = {}
        extra_dict = {}
        extra_dict['fq_name'] = rtr_obj.get_fq_name()

        rtr_q_dict['id'] = rtr_obj.uuid
        if not rtr_obj.display_name:
            rtr_q_dict['name'] = rtr_obj.get_fq_name()[-1]
        else:
            rtr_q_dict['name'] = rtr_obj.display_name
        rtr_q_dict['tenant_id'] = rtr_obj.parent_uuid.replace('-', '')
        rtr_q_dict['admin_state_up'] = rtr_obj.get_id_perms().enable
        rtr_q_dict['shared'] = False
        rtr_q_dict['status'] = constants.NET_STATUS_ACTIVE
        rtr_q_dict['gw_port_id'] = None

        ext_net_uuid = self._get_external_gateway_info(rtr_obj)
        if not ext_net_uuid:
            rtr_q_dict['external_gateway_info'] = None
        else:
            rtr_q_dict['external_gateway_info'] = {'network_id': ext_net_uuid,
                                                   'enable_snat': True}

        if self._contrail_extensions_enabled:
            rtr_q_dict.update(extra_dict)

        if rtr_obj.get_id_perms().get_description() is not None:
            rtr_q_dict['description'] = rtr_obj.get_id_perms().get_description()

        rtr_q_dict['created_at'] = rtr_obj.get_id_perms().get_created()
        rtr_q_dict['updated_at'] = rtr_obj.get_id_perms().get_last_modified()

        return rtr_q_dict
    #end _router_vnc_to_neutron

    def _check_port_fip_assoc(self, port_obj, fixed_ip_address, fip_obj):
        # check if port already has floating ip associated
        fip_refs = port_obj.get_floating_ip_back_refs()
        fip_ids= [ref['uuid'] for ref in fip_refs or []
                                  if ref['uuid'] != fip_obj.uuid]
        if fip_ids:
            fip_dict = self._vnc_lib.floating_ips_list(obj_uuids=fip_ids,
                        fields=['parent_uuid', 'floating_ip_fixed_ip_address'],
                        detail=False)
            fip_list = fip_dict.get('floating-ips')
            for fip in fip_list:
                if fip['floating_ip_fixed_ip_address'] == fixed_ip_address:
                    pool_obj = self._vnc_lib.floating_ip_pool_read(
                                             id=fip['parent_uuid'])
                    self._raise_contrail_exception(
                         'FloatingIPPortAlreadyAssociated',
                         floating_ip_address=fip_obj.get_floating_ip_address(),
                         fip_id=fip_obj.uuid, port_id=port_obj.uuid,
                         fixed_ip=fixed_ip_address, net_id=pool_obj.parent_uuid)

    def _floatingip_neutron_to_vnc(self, context, fip_q, oper):
        if oper == CREATE:
            # TODO for now create from default pool, later
            # use first available pool on net
            net_id = fip_q['floating_network_id']
            floating_ip_addr = fip_q.get('floating_ip_address')
            try:
                fq_name = self._fip_pool_list_network(net_id)[0]['fq_name']
            except IndexError:
                # IndexError could happens when an attempt to
                # retrieve a floating ip pool from a private network.
                msg = "Network %s doesn't provide a floatingip pool" % net_id
                self._raise_contrail_exception('BadRequest',
                                               resource="floatingip", msg=msg)
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fq_name)
            fip_name = str(fip_q.get('id', uuid.uuid4()))
            fip_obj = FloatingIp(fip_name, fip_pool_obj,
                                 floating_ip_address = floating_ip_addr)
            fip_obj.uuid = fip_name
            fip_obj.parent_uuid = fip_pool_obj.uuid

            proj_obj = self._get_project_obj(fip_q)
            perms2 = PermType2()
            perms2.owner = proj_obj.uuid
            perms2.owner_access = PERMS_RWX
            fip_obj.set_perms2(perms2)

            fip_obj.set_project(proj_obj)
        else:  # UPDATE
            try:
                fip_obj = self._vnc_lib.floating_ip_read(id=fip_q['id'])
            except NoIdError:
                self._raise_contrail_exception('FloatingIPNotFound',
                                               floatingip_id=fip_q['id'])

        port_id = fip_q.get('port_id')
        fixed_ip_address = fip_q.get('fixed_ip_address')
        if port_id:
            try:
                port_obj = self._virtual_machine_interface_read(port_id=port_id)
                port_tenant_id = self._get_obj_tenant_id('port', port_id)
                if context and not context['is_admin']:
                    if port_tenant_id != context['tenant'].replace('-', ''):
                        raise NoIdError(port_id)

                fip_proj_list = fip_obj.get_project_refs()
                if fip_proj_list:
                    fip_tenant_id = fip_proj_list[0].get('uuid').replace('-','')
                    if port_tenant_id != fip_tenant_id:
                        msg = "port can't be associated with this fip as they "\
                               "both belong to different tenants"
                        self._raise_contrail_exception('BadRequest',
                                             resource="floatingip", msg=msg)
            except NoIdError:
                self._raise_contrail_exception('PortNotFound',
                                               resource='floatingip',
                                               port_id=port_id)
            fip_obj.set_virtual_machine_interface(port_obj)
            #check for strict_compliance
            if self._strict_compliance:
                port_net_id = port_obj.get_virtual_network_refs()[0]['uuid']
                pvt_net_obj = self._vnc_lib.virtual_network_read(id=port_net_id)
                pvt_subnet_info = self._virtual_network_to_subnets(pvt_net_obj)
                pvt_subnet_id = pvt_subnet_info[0]['id']

                try:
                    fip_pool_obj = self._vnc_lib.floating_ip_pool_read(id=fip_obj.parent_uuid)
                except:
                    msg = "Network %s doesn't provide a floatingip pool" % port_net_id
                    self._raise_contrail_exception('BadRequest',
                                                   resource="floatingip", msg=msg)
                try:
                    ext_vn_obj = self._vnc_lib.virtual_network_read(id=fip_pool_obj.parent_uuid)
                except:
                    self._raise_contrail_exception('ExternalGatewayForFloatingIPNotFound',
                                                   subnet_id=pvt_subnet_id,
                                                   external_network_id=ext_vn_obj.uuid,
                                                   port_id=port_id)

                logical_router_refs = ext_vn_obj.get_logical_router_back_refs()
                ext_ports_uuid_list = []

                if logical_router_refs:
                    try:
                        for router_ref in (logical_router_refs):
                            ext_router_obj = self._vnc_lib.logical_router_read(id=router_ref['uuid'])
                            ext_ports_uuid_list += [vmi['uuid'] for vmi in (ext_router_obj.get_virtual_machine_interface_refs()or [])]
                    except:
                        self._raise_contrail_exception('ExternalGatewayForFloatingIPNotFound',
                                                       subnet_id=pvt_subnet_id,
                                                       external_network_id=ext_vn_obj.uuid,
                                                       port_id=port_id)

                pvt_port_objs = self._virtual_machine_interface_list(back_ref_id=port_net_id)
                pvt_ports_uuid_list = [port_obj.get_uuid() for port_obj in pvt_port_objs]
                pvt_net_port_set = set(pvt_ports_uuid_list)
                ext_net_port_set = set(ext_ports_uuid_list)

                if not pvt_net_port_set.intersection(ext_net_port_set):
                    self._raise_contrail_exception('ExternalGatewayForFloatingIPNotFound',
                                                   subnet_id=pvt_subnet_id,
                                                   external_network_id=ext_vn_obj.uuid,
                                                   port_id=port_id)
        elif 'port_id' in fip_q or (len(fip_q.keys()) == 1 and 'id' in fip_q):
            # port_id is empty or update called with empty body
            fip_obj.set_virtual_machine_interface_list([])

        if fixed_ip_address and port_id:
            self._check_port_fip_assoc(port_obj, fixed_ip_address, fip_obj)
            fip_obj.set_floating_ip_fixed_ip_address(fip_q['fixed_ip_address'])
        elif fixed_ip_address and not port_id:
            msg = "fixed_ip_address cannot be specified without a port_id"
            self._raise_contrail_exception('BadRequest',
                                           resource="floatingip", msg=msg)
        else:
            # fixed_ip_address not specified, pick from port_obj in create,
            # reset in case of disassociate
            port_refs = fip_obj.get_virtual_machine_interface_refs()
            if not port_refs:
                fip_obj.set_floating_ip_fixed_ip_address(None)
            else:
                port_obj = self._virtual_machine_interface_read(
                    port_id=port_refs[0]['uuid'],
                    fields=['instance_ip_back_refs', 'floating_ip_back_refs'])
                iip_refs = port_obj.get_instance_ip_back_refs()
                if len(iip_refs) > 1:
                    msg = 'Port %s has multiple fixed IP addresses.  Must ' \
                         'provide a specific IP address when assigning a ' \
                         'floating IP' % port_obj.uuid
                    self._raise_contrail_exception('BadRequest',
                                                   resource="floatingip",
                                                   msg=msg)
                if iip_refs:
                    iip_obj = self._instance_ip_read(
                                   instance_ip_id=iip_refs[0]['uuid'])
                    self._check_port_fip_assoc(
                         port_obj, iip_obj.get_instance_ip_address(), fip_obj)
                    fip_obj.set_floating_ip_fixed_ip_address(
                            iip_obj.get_instance_ip_address())
        if 'description' in fip_q:
            id_perms = fip_obj.get_id_perms()
            if id_perms:
                id_perms.set_description(fip_q['description'])
            else:
                id_perms = IdPermsType(enable=True,
                                       description=fip_q['description'])
            fip_obj.set_id_perms(id_perms)

        return fip_obj
    #end _floatingip_neutron_to_vnc

    @catch_convert_exception
    def _floatingip_vnc_to_neutron(self, fip_obj, memo_req=None, oper=READ):
        fip_q_dict = {}

        try:
            floating_net_id = memo_req['network_fqn'][tuple(fip_obj.get_fq_name()[:-2])]
        except (KeyError, TypeError):
            floating_net_id = self._vnc_lib.fq_name_to_id('virtual-network',
                                                 fip_obj.get_fq_name()[:-2])

        project_refs = fip_obj.get_project_refs()
        project_owner = fip_obj.get_perms2().get_owner()
        if project_refs is not None:
            tenant_id = project_refs[0]['uuid'].replace('-', '')
        elif is_uuid_like(project_owner):
            tenant_id = project_owner.replace('-', '')
        else:
            tenant_id = None

        port_id = None
        router_id = None
        port_obj = None
        port_refs = fip_obj.get_virtual_machine_interface_refs()

        for port_ref in port_refs or []:
            if memo_req:
                try:
                    port_obj = memo_req['ports'][port_ref['uuid']]
                except KeyError:
                    continue
            else:
                try:
                    port_obj = self._virtual_machine_interface_read(
                        port_id=port_ref['uuid'])
                except NoIdError:
                    continue

            # In case of floating ip on the Virtual-ip, svc-monitor will
            # link floating ip to "right" interface of service VMs
            # launched by ha-proxy service instance. Skip them
            props = port_obj.get_virtual_machine_interface_properties()
            if props:
                interface_type = props.get_service_interface_type()
                if interface_type == "right":
                    continue

            port_id = port_ref['uuid']
            break

        if tenant_id and port_obj:
            port_net_id = port_obj.get_virtual_network_refs()[0]['uuid']
            # find router_id from port
            if memo_req:
                router_list = memo_req['routers'].get(tenant_id, [])
            else:
                router_list = self._router_list_project(tenant_id, detail=True)

            vmi_routers = {}
            for router_obj in router_list or []:
                vmi_routers.update(dict((vmi_ref['uuid'], router_obj.uuid) for vmi_ref in (router_obj.get_virtual_machine_interface_refs() or [])))

            if memo_req:
                vmi_obj_list = [memo_req['ports'].get(vmi_id) for vmi_id in vmi_routers]
            else:
                if vmi_routers:
                    vmi_obj_list = self._virtual_machine_interface_list(
                        obj_uuids=vmi_routers.keys())
                else:
                    vmi_obj_list = []

            for vmi_obj in vmi_obj_list:
                if vmi_obj is None:
                    continue

                if (vmi_obj.get_virtual_network_refs()[0]['uuid'] ==
                    port_net_id):
                    router_id = vmi_routers[vmi_obj.uuid]
                    break

        fip_q_dict['id'] = fip_obj.uuid
        fip_q_dict['tenant_id'] = tenant_id
        fip_q_dict['floating_ip_address'] = fip_obj.get_floating_ip_address()
        fip_q_dict['floating_network_id'] = floating_net_id
        fip_q_dict['router_id'] = router_id
        fip_q_dict['port_id'] = port_id
        fip_q_dict['fixed_ip_address'] = fip_obj.get_floating_ip_fixed_ip_address()
        if port_obj:
            fip_q_dict['status'] = constants.PORT_STATUS_ACTIVE
        else:
            fip_q_dict['status'] = constants.PORT_STATUS_DOWN
        fip_q_dict['created_at'] = fip_obj.get_id_perms().get_created()
        fip_q_dict['updated_at'] = fip_obj.get_id_perms().get_last_modified()
        if fip_obj.get_id_perms().get_description() is not None:
            fip_q_dict['description'] = fip_obj.get_id_perms().get_description()

        return fip_q_dict
    #end _floatingip_vnc_to_neutron

    def _port_set_vm_instance(self, port_obj, instance_name, tenant_id,
                              baremetal=False):
        """ This function also deletes the old virtual_machine object
        associated with the port (if any) after the new virtual_machine
        object is associated with it.
        """
        vm_refs = port_obj.get_virtual_machine_refs()
        delete_vm_list = []
        for vm_ref in vm_refs or []:
            if vm_ref['to'] != [instance_name]:
                delete_vm_list.append(vm_ref)

        if instance_name:
            try:
                instance_obj = self._ensure_instance_exists(instance_name,
                                                            tenant_id,
                                                            baremetal)
                port_obj.set_virtual_machine(instance_obj)
            except RefsExistError as e:
                self._raise_contrail_exception('BadRequest',
                                               resource='port', msg=str(e))
        else:
            port_obj.set_virtual_machine_list([])

        if delete_vm_list:
            self._virtual_machine_interface_update(port_obj)
            for vm_ref in delete_vm_list:
                try:
                    self._resource_delete('virtual_machine', vm_ref['uuid'])
                except RefsExistError:
                    pass

    def _get_no_rule_security_group(self):
        sg_obj = self._vnc_lib.security_group_read(fq_name=SG_NO_RULE_FQ_NAME)

        return sg_obj
    # end _get_no_rule_security_group

    def _port_neutron_to_vnc(self, port_q, net_obj, oper):
        if oper == CREATE:
            project_id = str(uuid.UUID(port_q['tenant_id']))
            proj_obj = self._get_project_obj(port_q)
            id_perms = IdPermsType(enable=True)
            port_uuid = self._get_resource_id(port_q, True)
            if port_q.get('name'):
                port_name = port_q['name']
            else:
                port_name = port_uuid
            port_obj = VirtualMachineInterface(port_name, proj_obj,
                                               id_perms=id_perms)
            port_obj.uuid = port_uuid
            port_obj.set_virtual_network(net_obj)
            if ('mac_address' in port_q and port_q['mac_address']):
                mac_addrs_obj = MacAddressesType()
                mac_addrs_obj.set_mac_address([port_q['mac_address']])
                port_obj.set_virtual_machine_interface_mac_addresses(mac_addrs_obj)
        else:  # READ/UPDATE/DELETE
            port_obj = self._virtual_machine_interface_read(port_id=port_q['id'])
            project_id = self._get_obj_tenant_id('port', port_obj.get_uuid())

        if 'name' in port_q and port_q['name']:
            port_obj.display_name = port_q['name']

        if (port_q.get('device_owner') != constants.DEVICE_OWNER_ROUTER_GW
                and port_q.get('device_owner') not in constants.ROUTER_INTERFACE_OWNERS_SNAT
                and 'device_id' in port_q):
            # IRONIC: verify port associated to baremetal 'VM'
            if port_q.get('binding:vnic_type') == 'baremetal':
                self._port_set_vm_instance(port_obj, port_q.get('device_id'),
                                           project_id, baremetal=True)
            else:
                self._port_set_vm_instance(port_obj, port_q.get('device_id'),
                                           project_id)

        if 'device_owner' in port_q:
            port_obj.set_virtual_machine_interface_device_owner(port_q.get('device_owner'))

        # pick binding keys from neutron repr and persist as kvp elements
        # that are string/string(k/v).
        # it is assumed allowing/denying oper*key is done at neutron-server.
        vmi_binding_kvps = dict((k.replace('binding:',''), v)
            for k,v in port_q.items() if k.startswith('binding:'))
        for k,v in vmi_binding_kvps.items():
            if isinstance(v, basestring):
                continue
            vmi_binding_kvps[k] = json.dumps(v)

        if oper == CREATE:
            port_obj.set_virtual_machine_interface_bindings(
                KeyValuePairs([KeyValuePair(k,v)
                              for k,v in vmi_binding_kvps.items()]))
        elif oper == UPDATE:
            for k,v in vmi_binding_kvps.items():
                port_obj.add_virtual_machine_interface_bindings(
                    KeyValuePair(key=k, value=v))

            # Ironic may switch the mac address on a give port
            net_id = (port_q.get('network_id') or
                      port_obj.get_virtual_network_refs()[0]['uuid'])

            # Allow updating of mac addres for baremetal deployments or
            # when port is not attached to any VM
            allowed_port = (
                'binding:vnic_type' in port_q and port_q['binding:vnic_type'] == 'baremetal')
            if not allowed_port:
                port_bindings = port_obj.get_virtual_machine_interface_bindings()
                if port_bindings:
                    kvps = port_bindings.get_key_value_pair()
                else:
                    kvps = []
                for kvp in kvps:
                    if kvp.key == 'host_id' and kvp.value == "null":
                        allowed_port = True
                        break
            if 'mac_address' in port_q and allowed_port:
                # Ensure that duplicate mac address does not exist on this network
                ports = self._virtual_machine_interface_list(back_ref_id=net_id)
                for port in ports:
                    macs = port.get_virtual_machine_interface_mac_addresses()
                    for mac in macs.get_mac_address():
                        #ensure that the same address is not on any other port
                        if mac == port_q['mac_address'] and port.uuid != port_q['id']:
                            raise self._raise_contrail_exception("MacAddressInUse",
                            net_id=net_id, mac=port_q['mac_address'])
                # Update the mac accress if no duplicate found
                mac_addrs_obj = MacAddressesType()
                mac_addrs_obj.set_mac_address([port_q['mac_address']])
                port_obj.set_virtual_machine_interface_mac_addresses(mac_addrs_obj)

        if oper == CREATE:
            if 'port_security_enabled' in port_q:
                port_security = port_q['port_security_enabled']
            else:
                port_security = net_obj.get_port_security_enabled()
            port_obj.set_port_security_enabled(port_security)
            if ('security_groups' in port_q and
                port_q['security_groups'].__class__ is not object):
                if not port_security and port_q['security_groups']:
                    self._raise_contrail_exception(
                        'PortSecurityPortHasSecurityGroup',
                        port_id=port_obj.uuid)

                port_obj.set_security_group_list([])
                for sg_id in port_q.get('security_groups') or []:
                    try:
                        # TODO optimize to not read sg (only uuid/fqn needed)
                        sg_obj = self._vnc_lib.security_group_read(id=sg_id)
                    except NoIdError as e:
                        self._raise_contrail_exception(
                            'SecurityGroupNotFound', id=sg_id)
                    port_obj.add_security_group(sg_obj)

                # When there is no-security-group for a port,the internal
                # no_rule group should be used.
                if port_security and not port_q['security_groups']:
                    sg_obj = self._get_no_rule_security_group()
                    port_obj.add_security_group(sg_obj)
            elif port_security:
                sg_obj = SecurityGroup("default", proj_obj)
                port_obj.add_security_group(sg_obj)
        elif oper == UPDATE:
            if 'port_security_enabled' in port_q:
                port_obj.set_port_security_enabled(port_q['port_security_enabled'])
            port_security = port_obj.get_port_security_enabled()
            if port_security:
                if ('security_groups' in port_q and
                    port_q['security_groups'].__class__ is not object):
                    port_obj.set_security_group_list([])
                    if not port_q['security_groups']:
                        sg_obj = self._get_no_rule_security_group()
                        port_obj.add_security_group(sg_obj)
                    else:
                        for sg_id in port_q.get('security_groups') or []:
                            try:
                                # TODO optimize to not read sg (only uuid/fqn needed)
                                sg_obj = self._vnc_lib.security_group_read(id=sg_id)
                            except NoIdError as e:
                                self._raise_contrail_exception(
                                    'SecurityGroupNotFound', id=sg_id)
                            port_obj.add_security_group(sg_obj)
            else:
                if ('security_groups' in port_q and
                    port_q['security_groups'].__class__ is not object and
                    port_q['security_groups']):
                    self._raise_contrail_exception(
                        'PortSecurityAndIPRequiredForSecurityGroups',
                        port_id=port_obj.uuid)
                port_sg_refs = port_obj.get_security_group_refs()
                if port_sg_refs:
                    if 'security_groups' in port_q and not port_q['security_groups']:
                        # reset all SG on the port
                        port_obj.set_security_group_list([])
                    elif (len(port_sg_refs) == 1 and
                          port_sg_refs[0]['to'] == SG_NO_RULE_FQ_NAME):
                        port_obj.set_security_group_list([])
                    else:
                        self._raise_contrail_exception(
                            'PortSecurityPortHasSecurityGroup',
                            port_id=port_obj.uuid)

        id_perms = port_obj.get_id_perms()
        if 'admin_state_up' in port_q:
            id_perms.enable = port_q['admin_state_up']
            port_obj.set_id_perms(id_perms)

        if 'description' in port_q:
            id_perms = port_obj.get_id_perms()
            id_perms.set_description(port_q['description'])
            port_obj.set_id_perms(id_perms)

        if ('extra_dhcp_opts' in port_q):
            dhcp_options = []
            if port_q['extra_dhcp_opts']:
                for option_pair in port_q['extra_dhcp_opts']:
                    option = DhcpOptionType(
                        dhcp_option_name=option_pair['opt_name'],
                        dhcp_option_value=option_pair['opt_value'])
                    dhcp_options.append(option)
            if dhcp_options:
                olist = DhcpOptionsListType(dhcp_options)
                port_obj.set_virtual_machine_interface_dhcp_option_list(olist)
            else:
                port_obj.set_virtual_machine_interface_dhcp_option_list(None)

        if ('allowed_address_pairs' in port_q):
            aap_array = []
            if port_q['allowed_address_pairs']:
                for address_pair in port_q['allowed_address_pairs']:
                    mode = u'active-standby';
                    if 'mac_address' not in address_pair:
                        address_pair['mac_address'] = ""

                    cidr = address_pair['ip_address'].split('/')
                    if len(cidr) == 1:
                        if (IPAddress(cidr[0]).version == 4):
                            subnet=SubnetType(cidr[0], 32)
                        elif (IPAddress(cidr[0]).version == 6):
                            subnet=SubnetType(cidr[0], 128)
                    elif len(cidr) == 2:
                        subnet=SubnetType(cidr[0], int(cidr[1]));
                    else:
                        self._raise_contrail_exception(
                               'BadRequest', resource='port',
                               msg='Invalid address pair argument')
                    aap = AllowedAddressPair(subnet,
                                             address_pair['mac_address'], mode)
                    aap_array.append(aap)
            aaps = AllowedAddressPairs()
            if aap_array:
                aaps.set_allowed_address_pair(aap_array)
            port_obj.set_virtual_machine_interface_allowed_address_pairs(aaps)

        if 'fixed_ips' in port_q and port_q['fixed_ips'] is not None:
            net_id = (port_q.get('network_id') or
                      port_obj.get_virtual_network_refs()[0]['uuid'])
            port_obj_ips = None
            for fixed_ip in port_q.get('fixed_ips', []):
                if 'ip_address' in fixed_ip:
                    # read instance ip addrs on port only once
                    if port_obj_ips is None:
                        port_obj_ips = []
                        ip_back_refs = getattr(port_obj, 'instance_ip_back_refs', None)
                        if ip_back_refs:
                            for ip_back_ref in ip_back_refs:
                                try:
                                    ip_obj = self._instance_ip_read(
                                        instance_ip_id=ip_back_ref['uuid'])
                                except NoIdError:
                                    continue
                                port_obj_ips.append(ip_obj.get_instance_ip_address())
                    ip_addr = fixed_ip['ip_address']
                    if ip_addr in port_obj_ips:
                        continue
                    if oper == UPDATE:
                        self._raise_contrail_exception(
                            'BadRequest', resource='port',
                            msg='Fixed ip cannot be updated on a port')
                    if self._ip_addr_in_net_id(ip_addr, net_id):
                        self._raise_contrail_exception(
                            'IpAddressInUse', net_id=net_id,
                            ip_address=ip_addr)

        return port_obj
    # end _port_neutron_to_vnc

    @catch_convert_exception
    def _gw_port_vnc_to_neutron(self, port_obj, port_req_memo, oper=READ):
        vm_refs = port_obj.get_virtual_machine_refs()
        vm_uuid = vm_refs[0]['uuid']
        vm_obj = None
        try:
            vm_obj = port_req_memo['virtual-machines'][vm_uuid]
        except KeyError:
            try:
                vm_obj = self._vnc_lib.virtual_machine_read(id=vm_uuid)
            except NoIdError:
                return None
            port_req_memo['virtual-machines'][vm_uuid] = vm_obj

        si_refs = vm_obj.get_service_instance_refs()
        if not si_refs:
            return None

        try:
            si_obj = port_req_memo['service-instances'][si_refs[0]['uuid']]
        except KeyError:
            try:
                si_obj = self._vnc_lib.service_instance_read(id=si_refs[0]['uuid'],
                        fields=["logical_router_back_refs"])
            except NoIdError:
                return None

        rtr_back_refs = getattr(si_obj, "logical_router_back_refs", None)
        if not rtr_back_refs:
            return None
        return rtr_back_refs[0]['uuid']
    #end _gw_port_vnc_to_neutron

    def _get_router_gw_interface_for_neutron(self, context, router):
        si_ref = (router.get_service_instance_refs() or [None])[0]
        vn_ref = (router.get_virtual_network_refs() or [None])[0]
        if si_ref is None or vn_ref is None:
            # No gateway set on that router
            return

        # Router's gateway is enabled on the router
        # As Contrail model uses a service instance composed of 2 VM for the
        # gw stuff, we use the first VMI of the first SI's VM as Neutron router
        # gw port.
        # Only admin user or port's network owner can list router gw interface.
        if not context.get('is_admin', False):
            try:
                vn = self._vnc_lib.virtual_network_read(
                    id=vn_ref['uuid'], fields=['parent_uuid'],
                )
            except NoIdError:
                return
            if vn.parent_uuid != self._validate_project_ids(context)[0]:
                return

        try:
            si = self._vnc_lib.service_instance_read(
                id=si_ref['uuid'],
                fields=['virtual_machine_back_refs'],
            )
        except NoIdError:
            return

        # To be sure to always use the same SI's VM, we sort them by theirs
        # name
        sorted_vm_refs = sorted(si.get_virtual_machine_back_refs() or [],
                                key=lambda ref: ref['to'][-1])
        if len(sorted_vm_refs) >= 1:
            # And list right VM's VMIs. Return the first one (sorted by
            # name) but SI's VM habitually have only one right interface
            filters = {
                'virtual_machine_interface_properties':
                    json.dumps({'service_interface_type': "right"}),
            }
            vmis = self._virtual_machine_interface_list(
                back_ref_id=[sorted_vm_refs[0]['uuid']], filters=filters)
            sorted_vmis = sorted(vmis, key=lambda vmi: vmi.name)
            # Return first right VM's VMI if at least one IP is configured
            if (len(sorted_vmis) >= 1 and
                    sorted_vmis[0].get_instance_ip_back_refs()):
                return sorted_vmis[0]

    def _port_get_interface_status(self, port_obj):
        vmi_prop = port_obj.get_virtual_machine_interface_properties()
        if vmi_prop is None or vmi_prop.get_sub_interface_vlan_tag() is None:
            return constants.PORT_STATUS_DOWN
        # if the VMI is a subinterface do special handling.
        vmi_refs = port_obj.get_virtual_machine_interface_refs()
        if vmi_refs is None:
            return constants.PORT_STATUS_DOWN
        if len(vmi_refs) > 1:
            msg = ("Sub Interface %s(%s) has more that one VMI reference"
                   % (port_obj.get_fq_name_str(), port_obj.uuid))
            self.logger.warning(msg)
        # if parent interface of a sub interface is attached to a VM, then subinterface
        # is in PORT_STATUS_ACTIVE .
        parent_port_obj = self._virtual_machine_interface_read(port_id=vmi_refs[0]['uuid'],
                                                               fields=['virtual_machine_refs'])
        if parent_port_obj.get_virtual_machine_refs():
            return constants.PORT_STATUS_ACTIVE
        return constants.PORT_STATUS_DOWN

    @catch_convert_exception
    def _port_vnc_to_neutron(self, port_obj, port_req_memo=None, oper=READ):
        port_q_dict = {}
        extra_dict = {}
        extra_dict['fq_name'] = port_obj.get_fq_name()
        if not port_obj.display_name:
            # for ports created directly via vnc_api
            port_q_dict['name'] = port_obj.get_fq_name()[-1]
        else:
            port_q_dict['name'] = port_obj.display_name
        port_q_dict['id'] = port_obj.uuid

        net_refs = port_obj.get_virtual_network_refs()
        if net_refs:
            net_id = net_refs[0]['uuid']
        else:
            # TODO hack to force network_id on default port
            # as neutron needs it
            net_id = self._vnc_lib.obj_to_id(VirtualNetwork())

        if port_req_memo is None:
            # create a memo only for this port's conversion in this method
            port_req_memo = {}

        if 'networks' not in port_req_memo:
            port_req_memo['networks'] = {}
        if 'subnets' not in port_req_memo:
            port_req_memo['subnets'] = {}
        if 'virtual-machines' not in port_req_memo:
            port_req_memo['virtual-machines'] = {}
        if 'service-instances' not in port_req_memo:
            port_req_memo['service-instances'] = {}

        try:
            net_obj = port_req_memo['networks'][net_id]
        except KeyError:
            net_obj = self._virtual_network_read(net_id=net_id)
            port_req_memo['networks'][net_id] = net_obj
            subnets_info = self._virtual_network_to_subnets(net_obj)
            port_req_memo['subnets'][net_id] = subnets_info

        if port_obj.parent_type != "project":
            proj_id = net_obj.parent_uuid.replace('-', '')
        else:
            proj_id = port_obj.parent_uuid.replace('-', '')

        port_q_dict['tenant_id'] = proj_id
        port_q_dict['network_id'] = net_id

        # TODO RHS below may need fixing
        port_q_dict['mac_address'] = ''
        mac_refs = port_obj.get_virtual_machine_interface_mac_addresses()
        if mac_refs:
            port_q_dict['mac_address'] = mac_refs.mac_address[0]

        bindings = port_obj.get_virtual_machine_interface_bindings()
        if bindings:
            kvps = bindings.get_key_value_pair()
            for kvp in kvps:
                try:
                    port_q_dict['binding:'+kvp.key] = json.loads(kvp.value)
                except (ValueError, TypeError):
                    # native string case, so not stored as json
                    port_q_dict['binding:'+kvp.key] = kvp.value

        # 1. upgrade case, port created before bindings prop was
        #    defined on vmi OR
        # 2. defaults for keys needed by neutron
        if 'binding:vif_details' not in port_q_dict:
            port_q_dict['binding:vif_details'] = {'port_filter': True}
        if 'binding:vif_type' not in port_q_dict:
            port_q_dict['binding:vif_type'] = 'vrouter'
        if 'binding:vnic_type' not in port_q_dict:
            port_q_dict['binding:vnic_type'] = 'normal'
        if not port_q_dict.get('binding:host_id'):
            port_q_dict['binding:host_id'] = None
            port_q_dict['binding:vif_type'] = 'unbound'

        dhcp_options_list = port_obj.get_virtual_machine_interface_dhcp_option_list()
        if dhcp_options_list and dhcp_options_list.dhcp_option:
            dhcp_options = []
            for dhcp_option in dhcp_options_list.dhcp_option:
                # if dhcp_option_value_bytes is set, consider that value
                if dhcp_option.dhcp_option_value_bytes:
                    pair = {"opt_value": dhcp_option.dhcp_option_value_bytes,
                            "opt_name": dhcp_option.dhcp_option_name}
                else:
                    pair = {"opt_value": dhcp_option.dhcp_option_value,
                            "opt_name": dhcp_option.dhcp_option_name}
                dhcp_options.append(pair)
            port_q_dict['extra_dhcp_opts'] = dhcp_options

        allowed_address_pairs = port_obj.get_virtual_machine_interface_allowed_address_pairs()
        address_pairs = []
        if allowed_address_pairs and allowed_address_pairs.allowed_address_pair:
            for aap in allowed_address_pairs.allowed_address_pair:
                pair = {}
                pair["mac_address"] = aap.mac

                if IPAddress(aap.ip.get_ip_prefix()).version is 4:
                    ip_len = 32
                elif IPAddress(aap.ip.get_ip_prefix()).version is 6:
                    ip_len = 128

                if aap.ip.get_ip_prefix_len() == ip_len:
                    pair["ip_address"] = '%s' % (aap.ip.get_ip_prefix())
                else:
                    pair["ip_address"] = '%s/%s' % (aap.ip.get_ip_prefix(),
                                                 aap.ip.get_ip_prefix_len())
                address_pairs.append(pair)
        port_q_dict['allowed_address_pairs'] = address_pairs

        port_q_dict['fixed_ips'] = []
        ip_back_refs = getattr(port_obj, 'instance_ip_back_refs', None)
        if ip_back_refs:
            for ip_back_ref in ip_back_refs:
                primary_ip = True
                iip_uuid = ip_back_ref['uuid']
                # fetch it from request context cache/memo if there
                try:
                    ip_obj = port_req_memo['instance-ips'][iip_uuid]
                except KeyError:
                    try:
                        ip_obj = self._instance_ip_read(
                            instance_ip_id=ip_back_ref['uuid'])
                    except NoIdError:
                        continue

                ip_addr = ip_obj.get_instance_ip_address()
                ip_q_dict = {}
                ip_q_dict['ip_address'] = ip_addr
                if ip_addr:
                    ip_q_dict['subnet_id'] = ip_obj.get_subnet_uuid()

                if ip_obj.get_instance_ip_secondary():
                    primary_ip = False
                    try:
                        extra_dict['secondary_ips'].append(ip_q_dict)
                    except KeyError:
                        extra_dict['secondary_ips'] = [ip_q_dict]

                if ip_obj.get_service_health_check_ip():
                    primary_ip = False
                    try:
                        extra_dict['service_health_check_ips'].append(ip_q_dict)
                    except KeyError:
                        extra_dict['service_health_check_ips'] = [ip_q_dict]

                if ip_obj.get_service_instance_ip():
                    primary_ip = False

                    # If si_back_refs is not in the service_instance_ip, It is
                    # type v1 service_instance and service_instance_ip has to be
                    # added in the fixed_ip list
                    si_back_refs = ip_obj.get_service_instance_back_refs()
                    if not si_back_refs:
                        primary_ip = True

                    try:
                        extra_dict['service_instance_ips'].append(ip_q_dict)
                    except KeyError:
                        extra_dict['service_instance_ips'] = [ip_q_dict]

                if primary_ip:
                    port_q_dict['fixed_ips'].append(ip_q_dict)

        sg_refs = port_obj.get_security_group_refs() or []
        port_q_dict['security_groups'] = [ref['uuid'] for ref in sg_refs
                                          if ref['to'] != SG_NO_RULE_FQ_NAME]

        port_q_dict['admin_state_up'] = port_obj.get_id_perms().enable

        # port can be router interface or vm interface
        # for perf read logical_router_back_ref only when we have to
        port_parent_name = port_obj.parent_name
        router_refs = getattr(port_obj, 'logical_router_back_refs', None)
        if router_refs is not None:
            port_q_dict['device_id'] = router_refs[0]['uuid']
        elif port_obj.parent_type == 'virtual-machine':
            port_q_dict['device_id'] = port_obj.parent_name
        elif port_obj.get_virtual_machine_refs() is not None:
            rtr_uuid = self._gw_port_vnc_to_neutron(port_obj, port_req_memo)
            if rtr_uuid:
                port_q_dict['device_id'] = rtr_uuid
                port_q_dict['device_owner'] = constants.DEVICE_OWNER_ROUTER_GW
                # Neutron router gateway interface is a system resource only
                # visible by admin user or port's network owner. Neutron
                # intentionally set the tenant id to None for that
                # https://github.com/openstack/neutron/blob/master/neutron/db/l3_db.py#L354-L355
                # Not sure why it's necessary
                port_q_dict['tenant_id'] = ''
            else:
                port_q_dict['device_id'] = \
                    port_obj.get_virtual_machine_refs()[0]['to'][-1]
                port_q_dict['device_owner'] = ''
        else:
            port_q_dict['device_id'] = ''

        if not port_q_dict.get('device_owner'):
            port_q_dict['device_owner'] = \
                port_obj.get_virtual_machine_interface_device_owner() or '';
        if port_q_dict['device_id']:
            port_q_dict['status'] = constants.PORT_STATUS_ACTIVE
        else:
            port_q_dict['status'] = self._port_get_interface_status(port_obj)
        if self._contrail_extensions_enabled:
            port_q_dict.update(extra_dict)
        port_q_dict['port_security_enabled'] = port_obj.get_port_security_enabled()

        if port_obj.get_id_perms().get_description() is not None:
            port_q_dict['description'] = port_obj.get_id_perms().get_description()

        port_q_dict['created_at'] = port_obj.get_id_perms().get_created()
        port_q_dict['updated_at'] = port_obj.get_id_perms().get_last_modified()

        return port_q_dict
    #end _port_vnc_to_neutron

    def _port_get_host_prefixes(self, host_routes, subnet_cidr):
        """This function returns the host prefixes
        Eg. If host_routes have the below routes
           ---------------------------
           |destination   | next hop  |
           ---------------------------
           |  10.0.0.0/24 | 8.0.0.2   |
           |  12.0.0.0/24 | 10.0.0.4  |
           |  14.0.0.0/24 | 12.0.0.23 |
           |  16.0.0.0/24 | 8.0.0.4   |
           |  15.0.0.0/24 | 16.0.0.2  |
           |  20.0.0.0/24 | 8.0.0.12  |
           ---------------------------
           subnet_cidr is 8.0.0.0/24

           This function returns the dictionary
           '8.0.0.2' : ['10.0.0.0/24', '12.0.0.0/24', '14.0.0.0/24']
           '8.0.0.4' : ['16.0.0.0/24', '15.0.0.0/24']
           '8.0.0.12': ['20.0.0.0/24']
        """
        temp_host_routes = list(host_routes)
        cidr_ip_set = IPSet([subnet_cidr])
        host_route_dict = {}
        for route in temp_host_routes[:]:
            next_hop = route.get_next_hop()
            if IPAddress(next_hop) in cidr_ip_set:
                if next_hop in host_route_dict:
                    host_route_dict[next_hop].append(route.get_prefix())
                else:
                    host_route_dict[next_hop] = [route.get_prefix()]
                temp_host_routes.remove(route)

        # look for indirect routes
        if temp_host_routes:
            for ipaddr in host_route_dict:
                self._port_update_prefixes(host_route_dict[ipaddr],
                                           temp_host_routes)
        return host_route_dict

    def _port_update_prefixes(self, matched_route_list, unmatched_host_routes):
        process_host_routes = True
        while process_host_routes:
            process_host_routes = False
            for route in unmatched_host_routes:
                ip_addr = IPAddress(route.get_next_hop())
                if ip_addr in IPSet(matched_route_list):
                    matched_route_list.append(route.get_prefix())
                    unmatched_host_routes.remove(route)
                    process_host_routes = True

    def _port_check_and_add_iface_route_table(self, fixed_ips, net_obj,
                                              port_obj):
        ipam_refs = net_obj.get_network_ipam_refs()
        if not ipam_refs:
            return

        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                host_routes = subnet.get_host_routes()
                if host_routes is None:
                    continue
                sn_id = subnet.subnet_uuid
                subnet_cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                         subnet.subnet.get_ip_prefix_len())

                for ip_addr in [fixed_ip['ip_address'] for fixed_ip in
                                fixed_ips if fixed_ip['subnet_id'] == sn_id]:
                    host_prefixes = self._port_get_host_prefixes(host_routes.route,
                                                                 subnet_cidr)
                    if ip_addr in host_prefixes:
                        self._port_add_iface_route_table(host_prefixes[ip_addr],
                                                         port_obj, sn_id)

    def _port_add_iface_route_table(self, route_prefix_list, port_obj,
                                    subnet_id):
        project_obj = self._project_read(proj_id=port_obj.parent_uuid)
        intf_rt_name = '%s_%s_%s' % (_IFACE_ROUTE_TABLE_NAME_PREFIX,
                                     subnet_id, port_obj.uuid)
        intf_rt_fq_name = list(project_obj.get_fq_name())
        intf_rt_fq_name.append(intf_rt_name)

        try:
            intf_route_table_obj = self._vnc_lib.interface_route_table_read(
                                    fq_name=intf_rt_fq_name)
        except vnc_exc.NoIdError:
            route_table = RouteTableType(intf_rt_name)
            route_table.set_route([])
            intf_route_table = InterfaceRouteTable(
                                interface_route_table_routes=route_table,
                                parent_obj=project_obj,
                                name=intf_rt_name)

            intf_route_table_id = self._resource_create('interface_route_table',
                                    intf_route_table)
            intf_route_table_obj = self._vnc_lib.interface_route_table_read(
                                    id=intf_route_table_id)

        rt_routes = intf_route_table_obj.get_interface_route_table_routes()
        routes = rt_routes.get_route()
        # delete any old routes
        routes = []
        for prefix in route_prefix_list:
            routes.append(RouteType(prefix=prefix))
        rt_routes.set_route(routes)
        intf_route_table_obj.set_interface_route_table_routes(rt_routes)
        self._resource_update('interface_route_table', intf_route_table_obj)
        port_obj.add_interface_route_table(intf_route_table_obj)
        self._resource_update('virtual_machine_interface', port_obj)

    def _port_update_iface_route_table(self, net_obj, subnet_cidr, subnet_id,
                                       new_host_routes, old_host_routes=None):
        old_host_prefixes = {}
        if old_host_routes:
            old_host_prefixes = self._port_get_host_prefixes(old_host_routes.route,
                                                             subnet_cidr)
        new_host_prefixes = self._port_get_host_prefixes(new_host_routes,
                                                         subnet_cidr)

        for ipaddr, prefixes in old_host_prefixes.items():
            if ipaddr in new_host_prefixes:
                need_update = False
                if len(prefixes) == len(new_host_prefixes[ipaddr]):
                    for prefix in prefixes:
                        if prefix not in new_host_prefixes[ipaddr]:
                            need_update = True
                            break
                else:
                    need_update= True
                if need_update:
                    old_host_prefixes.pop(ipaddr)
                else:
                    # both the old and new are same. No need to do
                    # anything
                    old_host_prefixes.pop(ipaddr)
                    new_host_prefixes.pop(ipaddr)

        if not new_host_prefixes and not old_host_prefixes:
            # nothing to be done as old_host_routes and
            # new_host_routes match exactly
            return

        # get the list of all the ip objs for this network
        ipobjs = self._instance_ip_list(back_ref_id=[net_obj.uuid])
        for ipobj in ipobjs:
            ipaddr = ipobj.get_instance_ip_address()
            if ipaddr in old_host_prefixes:
                self._port_remove_iface_route_table(ipobj, subnet_id)
                continue

            if ipaddr in new_host_prefixes:
                port_back_refs = ipobj.get_virtual_machine_interface_refs()
                for port_ref in port_back_refs:
                    port_obj = self._virtual_machine_interface_read(
                                    port_id=port_ref['uuid'])
                    self._port_add_iface_route_table(new_host_prefixes[ipaddr],
                                                     port_obj, subnet_id)

    def _port_remove_iface_route_table(self, ipobj, subnet_id):
        port_refs = ipobj.get_virtual_machine_interface_refs()
        for port_ref in port_refs or []:
            port_obj = self._virtual_machine_interface_read(port_id=port_ref['uuid'])
            intf_rt_name = '%s_%s_%s' % (_IFACE_ROUTE_TABLE_NAME_PREFIX,
                                         subnet_id, port_obj.uuid)
            for rt_ref in port_obj.get_interface_route_table_refs() or []:
                if rt_ref['to'][2] != intf_rt_name:
                    continue
                try:
                    intf_route_table_obj = self._vnc_lib.interface_route_table_read(
                                                               id=rt_ref['uuid'])
                    port_obj.del_interface_route_table(intf_route_table_obj)
                    self._resource_update('virtual_machine_interface', port_obj)
                    self._resource_delete('interface_route_table', rt_ref['uuid'])
                except (NoIdError, RefsExistError) as e:
                    pass

    def _virtual_router_to_neutron(self, virtual_router):
        # TODO(md): Only dpdk enabled flag supported currently. Add more.
        dpdk_enabled = virtual_router.get_virtual_router_dpdk_enabled()

        # The .get_<resource>() method of VirtualRouter object seems to return
        # None in case a boolean is not set. Therefore the 'or False'
        # expression below to assure True or False values
        vr = {'dpdk_enabled': dpdk_enabled or False}
        return vr

    def wait_for_api_server_connection(func):
        def wrapper(self, *args, **kwargs):
            self._connected_to_api_server.wait()
            return func(self, *args, **kwargs)

        return wrapper
    # end wait_for_api_server_connection

    # public methods
    # network api handlers
    @wait_for_api_server_connection
    def network_create(self, network_q, context):
        net_obj = self._network_neutron_to_vnc(network_q, CREATE)
        try:
            net_uuid = self._resource_create('virtual_network', net_obj)
        except RefsExistError:
            self._raise_contrail_exception('BadRequest',
                resource='network', msg='Network Already exists')

        if net_obj.router_external:
            self._floating_ip_pool_create(net_obj)

        ret_network_q = self._network_vnc_to_neutron(net_obj, context=context)
        return ret_network_q
    # end network_create

    @wait_for_api_server_connection
    def network_read(self, net_uuid, fields=None, context=None):
        # see if we can return fast...
        #if fields and (len(fields) == 1) and fields[0] == 'tenant_id':
        #    tenant_id = self._get_obj_tenant_id('network', net_uuid)
        #    return {'id': net_uuid, 'tenant_id': tenant_id}

        try:
            net_obj = self._network_read(net_uuid)
        except NoIdError:
            self._raise_contrail_exception('NetworkNotFound', net_id=net_uuid)

        return self._network_vnc_to_neutron(net_obj, context=context)
    # end network_read

    @wait_for_api_server_connection
    def network_update(self, net_id, network_q, context):
        net_obj = self._virtual_network_read(net_id=net_id)
        router_external = net_obj.get_router_external()
        shared = net_obj.get_is_shared()
        network_q['id'] = net_id
        net_obj = self._network_neutron_to_vnc(network_q, UPDATE)
        if shared and not net_obj.is_shared:
            # If there are ports allocated outside of the project, do not allow
            # network setting to be changed from shared to not shared
            for vmi in net_obj.get_virtual_machine_interface_back_refs() or []:
                vmi_obj = self._virtual_machine_interface_read(port_id=vmi['uuid'])
                if (vmi_obj.parent_type == 'project' and
                    vmi_obj.parent_uuid != net_obj.parent_uuid):
                    self._raise_contrail_exception(
                        'InvalidSharedSetting',
                        network=net_obj.display_name)
        if net_obj.router_external and not router_external:
            self._floating_ip_pool_create(net_obj)
        elif router_external and not net_obj.router_external:
            fip_pools = net_obj.get_floating_ip_pools() or []
            for fip_pool in fip_pools:
                try:
                    self._floating_ip_pool_delete(fip_pool)
                except RefsExistError:
                    self._raise_contrail_exception('NetworkInUse',
                                                   net_id=net_id)
        self._virtual_network_update(net_obj)

        ret_network_q = self._network_vnc_to_neutron(net_obj, context=context)
        return ret_network_q
    # end network_update

    @wait_for_api_server_connection
    def network_delete(self, net_id, context):
        try:
            net_obj = self._vnc_lib.virtual_network_read(id=net_id)
        except NoIdError:
            return

        try:
            fip_pools = self._fip_pool_list_network(net_id, fields=['floating_ips'])
            for fip_pool in fip_pools or []:
                self._floating_ip_pool_delete(fip_pool=fip_pool)
            self._resource_delete('virtual_network', net_id)
        except RefsExistError:
            self._raise_contrail_exception('NetworkInUse', net_id=net_id)
        self._zookeeper_client.delete_node(
            '%s/%s' % (
                self.virtual_network_lock_prefix,net_id
            ))

    # end network_delete

    # TODO request based on filter contents
    @wait_for_api_server_connection
    def network_list(self, context=None, filters=None):
        ret_dict = {}

        def _collect_without_prune(net_ids):
            for net_id in net_ids:
                try:
                    net_obj = self._network_read(net_id)
                except NoIdError:
                    continue
                net_info = self._network_vnc_to_neutron(net_obj, oper=LIST, context=context)
                if net_info is None:
                    continue
                ret_dict[net_id] = net_info
        #end _collect_without_prune

        # collect phase
        all_net_objs = []  # all n/ws in all projects
        if context and not context['is_admin']:
            if filters and 'id' in filters:
                _collect_without_prune(filters['id'])
            elif filters and 'name' in filters:
                net_objs = self._network_list_project(context['tenant'])
                all_net_objs.extend(net_objs)
                all_net_objs.extend(self._network_list_filter(shared=True))
                all_net_objs.extend(self._network_list_filter(
                                    router_external=True))
            # if filters['shared'] is False get all the VNs in tenant_id
            # and prune the return list with shared = False or  shared = None
            elif (filters and 'shared' in filters  and filters['shared'] is True
                    or 'router:external' in filters):
                shared = None
                router_external = None
                if 'router:external' in filters:
                    router_external = filters['router:external'][0]
                if 'shared' in filters and filters['shared'] is True:
                    shared = filters['shared'][0]
                elif 'shared' in filters and filters['shared'] == False:
                    project_uuid = str(uuid.UUID(context['tenant']))
                    all_net_objs.extend(self._network_list_project(project_uuid))
                all_net_objs.extend(self._network_list_filter(
                                    shared, router_external))
            else:
                project_uuid = str(uuid.UUID(context['tenant']))
                if not filters:
                    all_net_objs.extend(self._network_list_filter(
                                 router_external=True))
                    all_net_objs.extend(self._network_list_filter(shared=True))
                all_net_objs.extend(self._network_list_project(project_uuid))
        # admin role from here on
        elif filters and 'tenant_id' in filters:
            # project-id is present
            if 'id' in filters:
                # required networks are also specified,
                # just read and populate ret_dict
                # prune is skipped because all_net_objs is empty
                _collect_without_prune(filters['id'])
            else:
                # read all networks in project, and prune below
                for p_id in self._validate_project_ids(context, filters) or []:
                    all_net_objs.extend(self._network_list_project(p_id))
                if 'router:external' in filters:
                    all_net_objs.extend(self._network_list_filter(
                                 router_external=filters['router:external'][0]))
        elif filters and 'id' in filters:
            # required networks are specified, just read and populate ret_dict
            # prune is skipped because all_net_objs is empty
            _collect_without_prune(filters['id'])
        elif filters and 'name' in filters:
            net_objs = self._network_list_project(None)
            all_net_objs.extend(net_objs)
        elif filters and 'shared' in filters or 'router:external' in filters:
            shared = None
            router_external = None
            if 'router:external' in filters:
                router_external = filters['router:external'][0]
            if 'shared' in filters:
                shared = filters['shared'][0]
            nets = self._network_list_filter(shared, router_external)
            for net in nets:
                net_info = self._network_vnc_to_neutron(net, oper=LIST, context=context)
                if net_info is None:
                    continue
                ret_dict[net.uuid] = net_info
        else:
            # read all networks in all projects
            all_net_objs.extend(self._virtual_network_list(detail=True))

        # prune phase
        for net_obj in all_net_objs:
            if net_obj.uuid in ret_dict:
                continue
            net_fq_name = str(net_obj.get_fq_name())
            if not self._filters_is_present(filters, 'fq_name',
                                            net_fq_name):
                continue
            if not self._filters_is_present(
                filters, 'name', net_obj.get_display_name() or net_obj.name):
                continue
            if net_obj.is_shared is None:
                is_shared = False
            elif net_obj.is_shared or (
                     net_obj.perms2 and
                     self._shared_with_tenant(context, net_obj.perms2.share)):
                is_shared = True
            else:
                is_shared = False

            if not self._filters_is_present(filters, 'shared',
                                            is_shared):
                continue
            net_info = self._network_vnc_to_neutron(net_obj, oper=LIST, context=context)
            if net_info is None:
                continue
            ret_dict[net_obj.uuid] = net_info

        ret_list = []
        for net in ret_dict.values():
            ret_list.append(net)

        return ret_list
    #end network_list

    def _resource_count_optimized(self, resource, filters=None):
        if filters and ('tenant_id' not in filters or len(filters.keys()) > 1):
            return None

        project_ids = filters.get('tenant_id') if filters else None
        if not isinstance(project_ids, list):
            project_ids = [project_ids]

        json_resource = resource.replace("_", "-")
        if resource == "floating_ips":
            count = lambda pid: self._vnc_lib.floating_ips_list(
                          back_ref_id=pid, count=True)[json_resource]['count']
        else:
            method = getattr(self._vnc_lib, resource + "_list")
            count = lambda pid: method(parent_id=pid,
                                       count=True)[json_resource]['count']

        ret = [count(pid) for pid in project_ids] if project_ids \
            else [count(None)]
        return sum(ret)

    # end _resource_count_optimized

    @wait_for_api_server_connection
    def network_count(self, filters=None, context=None):
        count = self._resource_count_optimized("virtual_networks", filters)
        if count is not None:
            return count

        nets_info = self.network_list(filters=filters)
        return len(nets_info)
    #end network_count

    # subnet api handlers

    def _subnet_get_vn_uuid(self,subnet_id):
        subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
        if not subnet_key:
            self._raise_contrail_exception('SubnetNotFound',
                                           subnet_id=subnet_id)
        return subnet_key.split()[0]

    def with_zookeper_vn_lock(func):

        def wrapper(self, *args, **kwargs):
            # Before we can update the rule inside virtual netowrk, get the
            # lock first. This lock will be released in finally block below.
            vn_id= args[0]
            scope_lock = self._zookeeper_client.lock(
                '%s/%s' % (
                self.virtual_network_lock_prefix, vn_id
                ))

            try:
                acquired_lock = scope_lock.acquire(timeout=_DEFAULT_ZK_LOCK_TIMEOUT)

                # If this node acquired the lock, continue with creation of
                # subnet
                return func(self, *args, **kwargs)

            except LockTimeout:
                # If the lock was not acquired and timeout of 5 seconds happened, then raise
                # a bad request error.
                self._api_server_obj.config_log("Zookeeper lock acquire timed out.",
                            level=SandeshLevel.SYS_INFO)
                msg = ("Subnet operation failed, Try again.. ")
                self._raise_contrail_exception('ServiceUnavailableError',
                    resource='subnet', msg=msg)
            finally:
                scope_lock.release()

        return wrapper
    # end with_zookeper_vn_lock

    @wait_for_api_server_connection
    @with_zookeper_vn_lock
    def _subnet_create(self, subnet_id,subnet_q):
        net_id = subnet_q['network_id']
        net_obj = self._virtual_network_read(net_id=net_id)

        ipam_fq_name = subnet_q.get('ipam_fq_name')
        if ipam_fq_name:
            domain_name, project_name, ipam_name = ipam_fq_name

            domain_obj = Domain(domain_name)
            project_obj = Project(project_name, domain_obj)
            netipam_obj = NetworkIpam(ipam_name, project_obj)
        else:  # link with project's default ipam or global default ipam
            try:
                ipam_fq_name = net_obj.get_fq_name()[:-1]
                ipam_fq_name.append('default-network-ipam')
                netipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            except NoIdError:
                netipam_obj = NetworkIpam()
            ipam_fq_name = netipam_obj.get_fq_name()

        subnet_vnc = self._subnet_neutron_to_vnc(subnet_q)
        subnet_prefix = self._subnet_vnc_get_prefix(subnet_vnc)

        # Locate list of subnets to which this subnet has to be appended
        net_ipam_ref = None
        ipam_refs = net_obj.get_network_ipam_refs()
        if ipam_refs:
            for ipam_ref in ipam_refs:
                if ipam_ref['to'] == ipam_fq_name:
                    net_ipam_ref = ipam_ref
                    break

        if not net_ipam_ref:
            # First link from net to this ipam
            vnsn_data = VnSubnetsType([subnet_vnc])
            net_obj.add_network_ipam(netipam_obj, vnsn_data)
        else:  # virtual-network already linked to this ipam
            for subnet in net_ipam_ref['attr'].get_ipam_subnets():
                if subnet_prefix == self._subnet_vnc_get_prefix(subnet):
                    existing_sn_id = subnet.subnet_uuid
                    # duplicate !!
                    msg = "Cidr %s overlaps with another subnet of subnet " \
                            "%s" % (subnet_q['cidr'], existing_sn_id)
                    self._raise_contrail_exception('BadRequest',
                                                   resource='subnet', msg=msg)
            vnsn_data = net_ipam_ref['attr']
            vnsn_data.ipam_subnets.append(subnet_vnc)
            # TODO: Add 'ref_update' API that will set this field
            net_obj._pending_field_updates.add('network_ipam_refs')
        try:
            self._virtual_network_update(net_obj)
        except OverQuota as e:
            self._raise_contrail_exception('OverQuota',
                overs=['subnet'], msg=str(e))

        # Read in subnet from server to get updated values for gw etc.
        subnet_vnc = self._subnet_read(net_id, subnet_prefix)
        subnet_info = self._subnet_vnc_to_neutron(subnet_vnc, net_obj,
                                                  ipam_fq_name)

        return subnet_info
    #end _subnet_create

    def subnet_create(self, subnet_q):
        net_id = subnet_q['network_id']
        return self._subnet_create(net_id,subnet_q)

    @wait_for_api_server_connection
    def subnet_read(self, subnet_id):
        subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
        if not subnet_key:
            self._raise_contrail_exception('SubnetNotFound',
                                           subnet_id=subnet_id)
        net_id = subnet_key.split()[0]

        try:
            net_obj = self._network_read(net_id)
        except NoIdError:
            self._raise_contrail_exception('SubnetNotFound',
                                           subnet_id=subnet_id)

        ipam_refs = net_obj.get_network_ipam_refs()
        if ipam_refs:
            for ipam_ref in ipam_refs:
                subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
                for subnet_vnc in subnet_vncs:
                    if subnet_vnc.subnet_uuid == subnet_id:
                        ret_subnet_q = self._subnet_vnc_to_neutron(
                            subnet_vnc, net_obj, ipam_ref['to'])
                        if ret_subnet_q is not None:
                            return ret_subnet_q

        self._raise_contrail_exception('SubnetNotFound',
                                       subnet_id=subnet_id)
    #end subnet_read

    def subnet_update(self, subnet_id, subnet_q):
        net_id = self._subnet_get_vn_uuid(subnet_id)
        return self._subnet_update(net_id, subnet_id,subnet_q)


    @wait_for_api_server_connection
    @with_zookeper_vn_lock
    def _subnet_update(self,net_id,subnet_id, subnet_q):
        if 'gateway_ip' in subnet_q:
            if subnet_q['gateway_ip'] != None:
                self._raise_contrail_exception(
                    'BadRequest', resource='subnet',
                    msg="update of gateway is not supported")

        if 'allocation_pools' in subnet_q:
            if subnet_q['allocation_pools'] != None:
                self._raise_contrail_exception(
                    'BadRequest', resource='subnet',
                    msg="update of allocation_pools is not allowed")

        net_obj = self._network_read(net_id)
        ipam_refs = net_obj.get_network_ipam_refs()
        subnet_found = False
        if ipam_refs:
            for ipam_ref in ipam_refs:
                subnets = ipam_ref['attr'].get_ipam_subnets()
                for subnet_vnc in subnets:
                    if subnet_vnc.subnet_uuid == subnet_id:
                        subnet_found = True
                        break
                if subnet_found:
                    if 'name' in subnet_q:
                        if subnet_q['name'] != None:
                            subnet_vnc.set_subnet_name(subnet_q['name'])
                    if 'gateway_ip' in subnet_q:
                        if subnet_q['gateway_ip'] != None:
                            subnet_vnc.set_default_gateway(subnet_q['gateway_ip'])

                    if 'enable_dhcp' in subnet_q:
                        if subnet_q['enable_dhcp'] != None:
                            subnet_vnc.set_enable_dhcp(subnet_q['enable_dhcp'])

                    if 'dns_nameservers' in subnet_q:
                        if subnet_q['dns_nameservers'] != None:
                            dhcp_options=[]
                            dns_servers=" ".join(subnet_q['dns_nameservers'])
                            if dns_servers:
                                dhcp_options.append(DhcpOptionType(dhcp_option_name='6',
                                                                   dhcp_option_value=dns_servers))
                            if dhcp_options:
                                subnet_vnc.set_dhcp_option_list(DhcpOptionsListType(dhcp_options))
                            else:
                                subnet_vnc.set_dhcp_option_list(None)

                    if 'host_routes' in subnet_q:
                        if subnet_q['host_routes'] != None:
                            host_routes=[]
                            for host_route in subnet_q['host_routes']:
                                host_routes.append(RouteType(prefix=host_route['destination'],
                                                             next_hop=host_route['nexthop']))
                            if self._apply_subnet_host_routes:
                                old_host_routes = subnet_vnc.get_host_routes()
                                subnet_cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                                                         subnet_vnc.subnet.get_ip_prefix_len())
                                self._port_update_iface_route_table(net_obj,
                                                                    subnet_cidr,
                                                                    subnet_id,
                                                                    host_routes,
                                                                    old_host_routes)
                            if host_routes:
                                subnet_vnc.set_host_routes(RouteTableType(host_routes))
                            else:
                                subnet_vnc.set_host_routes(None)

                    subnet_vnc.set_last_modified(
                               datetime.datetime.utcnow().isoformat())
                    net_obj._pending_field_updates.add('network_ipam_refs')
                    self._virtual_network_update(net_obj)
                    ret_subnet_q = self._subnet_vnc_to_neutron(
                                        subnet_vnc, net_obj, ipam_ref['to'])

                    return ret_subnet_q

        return {}
    # end subnet_update

    def subnet_delete(self, subnet_id):
        net_id = self._subnet_get_vn_uuid(subnet_id)
        return self._subnet_delete(net_id, subnet_id)

    @wait_for_api_server_connection
    @with_zookeper_vn_lock
    def _subnet_delete(self,net_id, subnet_id):

        net_obj = self._network_read(net_id)
        ipam_refs = net_obj.get_network_ipam_refs()
        if ipam_refs:
            for ipam_ref in ipam_refs:
                orig_subnets = ipam_ref['attr'].get_ipam_subnets()
                new_subnets = [subnet_vnc for subnet_vnc in orig_subnets
                               if subnet_vnc.subnet_uuid != subnet_id]
                if len(orig_subnets) != len(new_subnets):
                    # matched subnet to be deleted
                    ipam_ref['attr'].set_ipam_subnets(new_subnets)
                    net_obj._pending_field_updates.add('network_ipam_refs')
                    try:
                        self._vnc_lib.virtual_network_update(net_obj)
                    except RefsExistError:
                        self._raise_contrail_exception('SubnetInUse',
                                                       subnet_id=subnet_id)

                    return
    # end subnet_delete

    @wait_for_api_server_connection
    def subnets_list(self, context, filters=None):
        ret_subnets = []

        all_net_objs = []
        if filters and 'id' in filters:
            # required subnets are specified,
            # just read in corresponding net_ids
            net_ids = set([])
            for subnet_id in filters['id']:
                subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
                if not subnet_key:
                    continue
                net_ids.add(subnet_key.split()[0])
            all_net_objs.extend(self._virtual_network_list(obj_uuids=list(net_ids),
                                                           detail=True))
        elif (filters and 'shared' in filters  and filters['shared'] is True
                or 'router:external' in filters):
            shared = None
            router_external = None
            if 'router:external' in filters:
                router_external = filters['router:external'][0]
            if 'shared' in filters:
                shared = filters['shared'][0]
            net_objs = self._network_list_filter(shared, router_external)
            all_net_objs.extend(net_objs)
        else:
            if not context['is_admin']:
                proj_id = context['tenant']
            else:
                proj_id = None
            net_objs = self._network_list_project(proj_id)
            all_net_objs.extend(net_objs)
            net_objs = self._network_list_filter(shared=True)
            all_net_objs.extend(net_objs)

        ret_dict = {}
        for net_obj in all_net_objs:
            if net_obj.uuid in ret_dict:
                continue
            ret_dict[net_obj.uuid] = 1
            ipam_refs = net_obj.get_network_ipam_refs()
            if ipam_refs:
                for ipam_ref in ipam_refs:
                    subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
                    for subnet_vnc in subnet_vncs:
                        sn_info = self._subnet_vnc_to_neutron(subnet_vnc,
                                                              net_obj,
                                                              ipam_ref['to'],
                                                              oper=LIST)
                        if sn_info is None:
                            continue
                        sn_id = sn_info['id']
                        sn_proj_id = sn_info['tenant_id']
                        sn_net_id = sn_info['network_id']
                        sn_name = sn_info['name']

                        if (filters and 'shared' in filters and
                                        filters['shared'][0] == True):
                            if not net_obj.is_shared:
                                continue
                        elif filters:
                            if not self._filters_is_present(
                                    filters, 'id', sn_id):
                                continue
                            if not self._filters_is_present(
                                    filters, 'tenant_id', str(uuid.UUID(sn_proj_id))):
                                continue
                            if not self._filters_is_present(
                                    filters, 'network_id', sn_net_id):
                                continue
                            if not self._filters_is_present(
                                    filters, 'name', sn_name):
                                continue
                            if not self._filters_is_present(
                                    filters, 'cidr', sn_info['cidr']):
                                continue

                        ret_subnets.append(sn_info)

        return ret_subnets
    #end subnets_list

    @wait_for_api_server_connection
    def subnets_count(self, context, filters=None):
        subnets_info = self.subnets_list(context, filters)
        return len(subnets_info)
    #end subnets_count

    # ipam api handlers
    @wait_for_api_server_connection
    def ipam_create(self, ipam_q):
        # TODO remove below once api-server can read and create projects
        # from keystone on startup
        #self._ensure_project_exists(ipam_q['tenant_id'])

        ipam_obj = self._ipam_neutron_to_vnc(ipam_q, CREATE)
        try:
            ipam_uuid = self._resource_create('network_ipam', ipam_obj)
        except RefsExistError as e:
            self._raise_contrail_exception('BadRequest',
                resource='ipam', msg=str(e))
        return self._ipam_vnc_to_neutron(ipam_obj)
    #end ipam_create

    @wait_for_api_server_connection
    def ipam_read(self, ipam_id, oper=READ):
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(id=ipam_id)
        except NoIdError:
            # TO,DO add ipam specific exception
            self._raise_contrail_exception('NetworkNotFound',
                                           net_id=ipam_id)

        return self._ipam_vnc_to_neutron(ipam_obj, oper=oper)
    #end ipam_read

    @wait_for_api_server_connection
    def ipam_update(self, ipam_id, ipam_q):
        ipam_q['id'] = ipam_id
        ipam_obj = self._ipam_neutron_to_vnc(ipam_q, UPDATE)
        self._resource_update('network_ipam', ipam_obj)

        return self._ipam_vnc_to_neutron(ipam_obj)
    #end ipam_update

    @wait_for_api_server_connection
    def ipam_delete(self, ipam_id):
        self._resource_delete('network_ipam', ipam_id)
    #end ipam_delete

    # TODO request based on filter contents
    @wait_for_api_server_connection
    def ipam_list(self, context=None, filters=None):
        ret_list = []

        # collect phase
        all_ipams = []  # all ipams in all projects
        if filters and 'tenant_id' in filters:
            for p_id in self._validate_project_ids(context, filters) or []:
                project_ipams = self._ipam_list_project(p_id)
                all_ipams.append(project_ipams)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_ipams = self._ipam_list_project(proj_id)
                all_ipams.append(project_ipams)

        # prune phase
        for project_ipams in all_ipams:
            for proj_ipam in project_ipams:
                # TODO implement same for name specified in filter
                proj_ipam_id = proj_ipam['uuid']
                if not self._filters_is_present(filters, 'id', proj_ipam_id):
                    continue
                ipam_info = self.ipam_read(proj_ipam['uuid'], oper=LIST)
                if ipam_info is None:
                    continue
                ret_list.append(ipam_info)

        return ret_list
    #end ipam_list

    @wait_for_api_server_connection
    def ipam_count(self, filters=None):
        count = self._resource_count_optimized("network_ipams", filters)
        if count is not None:
            return count

        ipam_info = self.ipam_list(filters=filters)
        return len(ipam_info)
    #end ipam_count

    # policy api handlers
    @wait_for_api_server_connection
    def policy_create(self, policy_q):
        # TODO remove below once api-server can read and create projects
        # from keystone on startup
        #self._ensure_project_exists(policy_q['tenant_id'])

        policy_obj = self._policy_neutron_to_vnc(policy_q, CREATE)
        try:
            policy_uuid = self._resource_create('network_policy', policy_obj)
        except (RefsExistError, BadRequest) as e:
            self._raise_contrail_exception('BadRequest',
                resource='policy', msg=str(e))
        return self._policy_vnc_to_neutron(policy_obj)
    #end policy_create

    @wait_for_api_server_connection
    def policy_read(self, policy_id, oper=READ):
        try:
            policy_obj = self._vnc_lib.network_policy_read(id=policy_id)
        except NoIdError:
            raise policy.PolicyNotFound(id=policy_id)

        return self._policy_vnc_to_neutron(policy_obj, oper=oper)
    #end policy_read

    @wait_for_api_server_connection
    def policy_update(self, policy_id, policy):
        policy_q = policy
        policy_q['id'] = policy_id
        policy_obj = self._policy_neutron_to_vnc(policy_q, UPDATE)
        self._resource_update('network_policy', policy_obj)

        return self._policy_vnc_to_neutron(policy_obj)
    #end policy_update

    @wait_for_api_server_connection
    def policy_delete(self, policy_id):
        self._resource_delete('network_policy', policy_id)
    #end policy_delete

    # TODO request based on filter contents
    @wait_for_api_server_connection
    def policy_list(self, context=None, filters=None):
        ret_list = []

        # collect phase
        all_policys = []  # all policys in all projects
        if filters and 'tenant_id' in filters:
            for p_id in self._validate_project_ids(context, filters) or []:
                project_policys = self._policy_list_project(p_id)
                all_policys.append(project_policys)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_policys = self._policy_list_project(proj_id)
                all_policys.append(project_policys)

        # prune phase
        for project_policys in all_policys:
            for proj_policy in project_policys:
                # TODO implement same for name specified in filter
                proj_policy_id = proj_policy['uuid']
                if not self._filters_is_present(filters, 'id', proj_policy_id):
                    continue
                policy_info = self.policy_read(proj_policy['uuid'], oper=LIST)
                if policy_info is None:
                    continue
                ret_list.append(policy_info)

        return ret_list
    #end policy_list

    @wait_for_api_server_connection
    def policy_count(self, filters=None):
        count = self._resource_count_optimized("network_policys", filters)
        if count is not None:
            return count

        policy_info = self.policy_list(filters=filters)
        return len(policy_info)
    #end policy_count

    def _router_update_gateway(self, router_q, rtr_obj):
        ext_gateway = router_q.get('external_gateway_info', None)
        old_ext_gateway = self._get_external_gateway_info(rtr_obj)
        if ext_gateway or old_ext_gateway:
            network_id = None
            if ext_gateway:
                network_id = ext_gateway.get('network_id', None)
            if network_id:
                if old_ext_gateway and network_id == old_ext_gateway:
                    return
                try:
                    net_obj = self._virtual_network_read(net_id=network_id)
                    if not net_obj.get_router_external():
                        self._raise_contrail_exception(
                            'BadRequest', resource='router',
                            msg="Network %s is not a valid external network" % network_id)
                except NoIdError:
                    self._raise_contrail_exception('NetworkNotFound',
                                                   net_id=network_id)

                self._router_set_external_gateway(rtr_obj, net_obj)
            else:
                self._router_clear_external_gateway(rtr_obj)

    def _router_set_external_gateway(self, router_obj, ext_net_obj):
        # Set logical gateway virtual network
        router_obj.set_virtual_network(ext_net_obj)
        self._resource_update('logical_router', router_obj)

    def _router_clear_external_gateway(self, router_obj):
        # Clear logical gateway virtual network
        router_obj.set_virtual_network_list([])
        self._resource_update('logical_router', router_obj)

    # router api handlers
    @wait_for_api_server_connection
    def router_create(self, router_q):
        #self._ensure_project_exists(router_q['tenant_id'])

        rtr_obj = self._router_neutron_to_vnc(router_q, CREATE)
        rtr_uuid = self._resource_create('logical_router', rtr_obj)
        # read it back to update id perms
        rtr_obj = self._logical_router_read(rtr_uuid)
        self._router_update_gateway(router_q, rtr_obj)
        ret_router_q = self._router_vnc_to_neutron(rtr_obj)

        return ret_router_q
    #end router_create

    @wait_for_api_server_connection
    def router_read(self, rtr_uuid, fields=None):
        # see if we can return fast...
        if fields and (len(fields) == 1) and fields[0] == 'tenant_id':
            tenant_id = self._get_obj_tenant_id('router', rtr_uuid)
            return {'id': rtr_uuid, 'tenant_id': tenant_id}

        try:
            rtr_obj = self._logical_router_read(rtr_uuid)
        except NoIdError:
            self._raise_contrail_exception('RouterNotFound',
                                           router_id=rtr_uuid)

        return self._router_vnc_to_neutron(rtr_obj)
    #end router_read

    @wait_for_api_server_connection
    def router_update(self, rtr_id, router_q):
        router_q['id'] = rtr_id
        rtr_obj = self._router_neutron_to_vnc(router_q, UPDATE)
        self._logical_router_update(rtr_obj)
        self._router_update_gateway(router_q, rtr_obj)
        ret_router_q = self._router_vnc_to_neutron(rtr_obj)

        return ret_router_q
    #end router_update

    @wait_for_api_server_connection
    def router_delete(self, rtr_id):
        try:
            rtr_obj = self._logical_router_read(rtr_id)
            if rtr_obj.get_virtual_machine_interface_refs():
                self._raise_contrail_exception('RouterInUse',
                                               router_id=rtr_id)
        except NoIdError:
            self._raise_contrail_exception('RouterNotFound',
                                           router_id=rtr_id)

        self._router_clear_external_gateway(rtr_obj)
        self._logical_router_delete(rtr_id=rtr_id)
    #end router_delete

    # TODO request based on filter contents
    @wait_for_api_server_connection
    def router_list(self, context=None, filters=None):
        ret_list = []

        if filters and 'shared' in filters:
            if filters['shared'][0] == True:
                # no support for shared routers
                return ret_list

        # collect phase
        all_rtrs = []  # all n/ws in all projects
        if filters and 'tenant_id' in filters:
            # project-id is present
            if 'id' in filters:
                # required routers are also specified,
                # just read and populate ret_list
                # prune is skipped because all_rtrs is empty
                for rtr_id in filters['id']:
                    try:
                        rtr_obj = self._logical_router_read(rtr_id)
                    except NoIdError:
                        continue
                    rtr_info = self._router_vnc_to_neutron(rtr_obj, oper=LIST)
                    if rtr_info is None:
                        continue
                    ret_list.append(rtr_info)
            else:
                # read all routers in project, and prune below
                for p_id in self._validate_project_ids(context, filters) or []:
                    if 'router:external' in filters:
                        all_rtrs.append(self._fip_pool_ref_routers(p_id))
                    else:
                        project_rtrs = self._router_list_project(p_id)
                        all_rtrs.append(project_rtrs)
        elif filters and 'id' in filters:
            # required routers are specified, just read and populate ret_list
            # prune is skipped because all_rtrs is empty
            for rtr_id in filters['id']:
                try:
                    rtr_obj = self._logical_router_read(rtr_id)
                except NoIdError:
                    continue
                rtr_info = self._router_vnc_to_neutron(rtr_obj, oper=LIST)
                if rtr_info is None:
                    continue
                ret_list.append(rtr_info)
        else:
            if not context['is_admin']:
                project_id = str(uuid.UUID(context['tenant']))
            else:
                project_id = None

            # read all routers in specified projects
            project_rtrs = self._router_list_project(project_id=project_id)
            all_rtrs.append(project_rtrs)

        # prune phase
        for project_rtrs in all_rtrs:
            for proj_rtr in project_rtrs:
                proj_rtr_id = proj_rtr['uuid']
                if not self._filters_is_present(filters, 'id', proj_rtr_id):
                    continue

                proj_rtr_fq_name = str(proj_rtr['fq_name'])
                if not self._filters_is_present(filters, 'fq_name',
                                                proj_rtr_fq_name):
                    continue
                try:
                    rtr_obj = self._logical_router_read(proj_rtr['uuid'])
                except NoIdError:
                    continue
                rtr_info = self._router_vnc_to_neutron(rtr_obj, oper=LIST)
                if rtr_info is None:
                    continue
                if not self._filters_is_present(filters, 'name',
                        rtr_obj.get_display_name() or rtr_obj.name):
                    continue
                ret_list.append(rtr_info)

        return ret_list
    #end router_list

    @wait_for_api_server_connection
    def router_count(self, filters=None):
        count = self._resource_count_optimized("logical_routers", filters)
        if count is not None:
            return count

        rtrs_info = self.router_list(filters=filters)
        return len(rtrs_info)
    #end router_count

    def _check_for_dup_router_subnet(self, router_id,
                                     network_id, subnet_id, subnet_cidr):
        try:
            rports = self.port_list(filters={'device_id': [router_id]})
            # It's possible these ports are on the same network, but
            # different subnets.
            new_ipnet = netaddr.IPNetwork(subnet_cidr)
            for p in rports:
                for ip in p['fixed_ips']:
                    if ip['subnet_id'] == subnet_id:
                       msg = "Router %s already has a port " \
                                "on subnet %s" % (router_id, subnet_id)
                       self._raise_contrail_exception(
                           'BadRequest', resource='router', msg=msg)
                    sub_id = ip['subnet_id']
                    subnet = self.subnet_read(sub_id)
                    cidr = subnet['cidr']
                    ipnet = netaddr.IPNetwork(cidr)
                    match1 = netaddr.all_matching_cidrs(new_ipnet, [cidr])
                    match2 = netaddr.all_matching_cidrs(ipnet, [subnet_cidr])
                    if match1 or match2:
                        data = {'subnet_cidr': subnet_cidr,
                                'subnet_id': subnet_id,
                                'cidr': cidr,
                                'sub_id': sub_id}
                        msg = "Cidr %(subnet_cidr)s of subnet " \
                                 "%(subnet_id)s overlaps with cidr %(cidr)s " \
                                 "of subnet %(sub_id)s" % data
                        self._raise_contrail_exception(
                            'BadRequest', resource='router', msg=msg)
        except NoIdError:
            pass

    @wait_for_api_server_connection
    def add_router_interface(self, context, router_id, port_id=None, subnet_id=None):
        router_obj = self._logical_router_read(router_id)
        if port_id:
            port = self.port_read(port_id)
            if (not context.get('is_admin', False) and
                    port['tenant_id'] != context['tenant_id'].replace('-', '')):
                self._raise_contrail_exception('RouterInterfaceNotFound',
                                               router_id=router_id,
                                               port_id=port_id)
            if (port['device_owner'] == constants.DEVICE_OWNER_ROUTER_INTF and
                    port['device_id']):
                self._raise_contrail_exception('PortInUse',
                                               net_id=port['network_id'],
                                               port_id=port['id'],
                                               device_id=port['device_id'])
            fixed_ips = [ip for ip in port['fixed_ips']]
            if len(fixed_ips) != 1:
                self._raise_contrail_exception(
                    'BadRequest', resource='router',
                    msg='Router port must have exactly one fixed IP')
            subnet_id = fixed_ips[0]['subnet_id']
            subnet = self.subnet_read(subnet_id)
            self._check_for_dup_router_subnet(router_id,
                                              port['network_id'],
                                              subnet['id'],
                                              subnet['cidr'])

        elif subnet_id:
            subnet = self.subnet_read(subnet_id)
            if (not context.get('is_admin', False) and
                    subnet['tenant_id'] !=
                    context['tenant_id'].replace('-', '')):
                self._raise_contrail_exception(
                     'RouterInterfaceNotFoundForSubnet',
                     router_id=router_id,
                     subnet_id=subnet_id)
            if not subnet['gateway_ip']:
                self._raise_contrail_exception(
                    'BadRequest', resource='router',
                    msg='Subnet for router interface must have a gateway IP')
            self._check_for_dup_router_subnet(router_id,
                                              subnet['network_id'],
                                              subnet_id,
                                              subnet['cidr'])

            fixed_ip = {'ip_address': subnet['gateway_ip'],
                        'subnet_id': subnet['id']}
            port = self.port_create(context, {'tenant_id': subnet['tenant_id'],
                 'network_id': subnet['network_id'],
                 'fixed_ips': [fixed_ip],
                 'admin_state_up': True,
                 'device_id': router_id,
                 'device_owner': constants.DEVICE_OWNER_ROUTER_INTF,
                 'name': '',
                 'port_security_enabled': False})

            port_id = port['id']

        else:
            self._raise_contrail_exception(
                'BadRequest', resource='router',
                msg='Either port or subnet must be specified')

        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=port_id)
        vmi_obj.set_virtual_machine_interface_device_owner(
            constants.DEVICE_OWNER_ROUTER_INTF)
        self._resource_update('virtual_machine_interface', vmi_obj)
        router_obj.add_virtual_machine_interface(vmi_obj)
        self._logical_router_update(router_obj)
        info = {'id': router_id,
                'tenant_id': subnet['tenant_id'],
                'port_id': port_id,
                'subnet_id': subnet_id}
        return info
    # end add_router_interface

    @wait_for_api_server_connection
    def remove_router_interface(self, router_id, port_id=None, subnet_id=None):
        router_obj = self._logical_router_read(router_id)
        subnet = None
        if port_id:
            port_db = self.port_read(port_id)
            if (port_db['device_owner'] not in
                    [constants.DEVICE_OWNER_ROUTER_INTF,
                     constants.DEVICE_OWNER_DVR_INTERFACE]
                    or port_db['device_id'] != router_id):
                self._raise_contrail_exception('RouterInterfaceNotFound',
                                               router_id=router_id,
                                               port_id=port_id)
            port_subnet_id = port_db['fixed_ips'][0]['subnet_id']
            if subnet_id and (port_subnet_id != subnet_id):
                self._raise_contrail_exception('SubnetMismatchForPort',
                                               port_id=port_id,
                                               subnet_id=subnet_id)
            subnet_id = port_subnet_id
            subnet = self.subnet_read(subnet_id)
            network_id = subnet['network_id']
        elif subnet_id:
            subnet = self.subnet_read(subnet_id)
            network_id = subnet['network_id']

            for intf in router_obj.get_virtual_machine_interface_refs() or []:
                port_id = intf['uuid']
                port_db = self.port_read(port_id)
                if subnet_id == port_db['fixed_ips'][0]['subnet_id']:
                    break
            else:
                self._raise_contrail_exception('RouterInterfaceNotFoundForSubnet',
                                               router_id=router_id,
                                               subnet_id=subnet_id)

        port_obj = self._virtual_machine_interface_read(port_id)
        router_obj.del_virtual_machine_interface(port_obj)
        self._resource_update('logical_router', router_obj)
        self.port_delete(port_id)
        info = {'id': router_id,
            'tenant_id': subnet['tenant_id'],
            'port_id': port_id,
            'subnet_id': subnet_id}
        return info
    # end remove_router_interface

    # floatingip api handlers
    @wait_for_api_server_connection
    def floatingip_create(self, context, fip_q):
        fip_obj = self._floatingip_neutron_to_vnc(context, fip_q, CREATE)
        try:
            fip_uuid = self._resource_create('floating_ip', fip_obj)
        except OverQuota as e:
            self._raise_contrail_exception('OverQuota',
                overs=['floatingip'], msg=str(e))
        except Exception as e:
            self._raise_contrail_exception('IpAddressGenerationFailure',
                                           net_id=fip_q['floating_network_id'])
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_uuid)

        return self._floatingip_vnc_to_neutron(fip_obj)
    #end floatingip_create

    @wait_for_api_server_connection
    def floatingip_read(self, fip_uuid):
        try:
            fip_obj = self._vnc_lib.floating_ip_read(id=fip_uuid)
        except NoIdError:
            self._raise_contrail_exception('FloatingIPNotFound',
                                           floatingip_id=fip_uuid)

        return self._floatingip_vnc_to_neutron(fip_obj)
    #end floatingip_read

    @wait_for_api_server_connection
    def floatingip_update(self, context, fip_id, fip_q):
        fip_q['id'] = fip_id
        fip_obj = self._floatingip_neutron_to_vnc(context, fip_q, UPDATE)
        self._resource_update('floating_ip', fip_obj)

        return self._floatingip_vnc_to_neutron(fip_obj)
    #end floatingip_update

    @wait_for_api_server_connection
    def floatingip_delete(self, fip_id):
        try:
            self._resource_delete('floating_ip', fip_id)
        except NoIdError:
            self._raise_contrail_exception('FloatingIPNotFound',
                                           floatingip_id=fip_id)
    #end floatingip_delete

    @wait_for_api_server_connection
    def floatingip_list(self, context, filters=None):
        # Read in floating ips with either
        # - port(s) as anchor
        # - project(s) as anchor
        # - none as anchor (floating-ip collection)
        ret_list = []

        proj_ids = None
        port_ids = None
        fip_ids = None
        backref_ids = None
        if filters:
            if 'id' in filters:
                fip_ids = [str(uuid.UUID(fid)) for fid in filters['id']]
            if 'tenant_id' in filters:
                backref_ids = self._validate_project_ids(context, filters)
                proj_ids = backref_ids or []
            if 'port_id' in filters:
                port_ids = [str(uuid.UUID(pid)) for pid in filters['port_id']]
                if len(port_ids) > 0:
                    backref_ids = port_ids

        if not context['is_admin']:
            backref_ids = [str(uuid.UUID(context['tenant']))]

        memo_req = {'routers': {},
                    'ports': {},
                    'network_fqn':{}}

        fip_objs = self._vnc_lib.floating_ips_list(obj_uuids=fip_ids,
                                                   back_ref_id=backref_ids,
                                                   detail=True)
        if not fip_objs:
            return []
        # prep memo for optimization
        fip_vn_fqn = set(tuple(fip_obj.fq_name[:-2]) for fip_obj in fip_objs)
        for vn_fqn in fip_vn_fqn:
            try:
                memo_req['network_fqn'][vn_fqn] = self._vnc_lib.fq_name_to_id(
                    'virtual-network', vn_fqn)
            except NoIdError:
                pass

        fip_project_refs = []
        for fip_obj in fip_objs:
            ref = fip_obj.get_project_refs()
            if ref:
                fip_project_refs.append(ref[0]['uuid'])
            else:
                fip_project_refs.append(fip_obj.get_perms2().get_owner())

        lr_objs = self._logical_router_list(parent_id=fip_project_refs)
        for lr_obj in lr_objs:
            tenant_id = lr_obj.parent_uuid.replace('-', '')
            try:
                memo_req['routers'][tenant_id].append(lr_obj)
            except KeyError:
                memo_req['routers'][tenant_id] = [lr_obj]

        vmi_uuids = []
        for fip_obj in fip_objs:
            vmi_uuids.extend([ref['uuid'] for ref in
                fip_obj.get_virtual_machine_interface_refs() or []])
        for lr_obj in lr_objs:
            vmi_uuids.extend([ref['uuid'] for ref in
                lr_obj.get_virtual_machine_interface_refs() or []])
        vmi_objs = self._virtual_machine_interface_list(obj_uuids=vmi_uuids)
        memo_req['ports'] = dict((vmi_obj.uuid, vmi_obj) for vmi_obj in vmi_objs)

        # prepare result in neutron form and return
        for fip_obj in fip_objs:
            if 'floating_ip_address' in filters:
                if (fip_obj.get_floating_ip_address() not in
                        filters['floating_ip_address']):
                    continue
            # if filters has both id and tenant_id, api-server would
            # have returned ORed value, neutron expects ANDed
            if filters and 'id' in filters and fip_obj.uuid not in fip_ids:
                continue
            if filters and 'tenant_id' in filters:
                if not fip_obj.get_project_refs():
                    continue
                if fip_obj.get_project_refs()[0]['uuid'] not in proj_ids:
                    continue
            if filters and 'port_id' in filters:
                if not fip_obj.get_virtual_machine_interface_refs():
                    continue
                if fip_obj.get_virtual_machine_interface_refs(
                       )[0]['uuid'] not in port_ids:
                    continue

            fip_info = self._floatingip_vnc_to_neutron(fip_obj, memo_req,
                                                       oper=LIST)
            if fip_info is None:
                continue
            ret_list.append(fip_info)

        return ret_list
    #end floatingip_list

    @wait_for_api_server_connection
    def floatingip_count(self, context, filters=None):
        count = self._resource_count_optimized("floating_ips", filters)
        if count is not None:
            return count

        floatingip_info = self.floatingip_list(context, filters)
        return len(floatingip_info)
    #end floatingip_count

    def _ip_addr_in_net_id(self, ip_addr, net_id):
        """Checks if ip address is present in net-id."""
        net_ip_list = [ipobj.get_instance_ip_address() for ipobj in
                                self._instance_ip_list(back_ref_id=[net_id])]
        return ip_addr in net_ip_list

    def _create_instance_ip(self, net_obj, port_obj, ip_addr=None,
                            subnet_uuid=None, ip_family=None):
        ip_name = str(uuid.uuid4())
        ip_obj = InstanceIp(name=ip_name)
        ip_obj.uuid = ip_name
        if subnet_uuid:
            ip_obj.set_subnet_uuid(subnet_uuid)
        ip_obj.set_virtual_machine_interface(port_obj)
        ip_obj.set_virtual_network(net_obj)
        ip_obj.set_instance_ip_family(ip_family)
        if ip_addr:
            ip_obj.set_instance_ip_address(ip_addr)

        # set instance ip ownership to real tenant
        perms2 = PermType2()
        tenant_id = self._get_obj_tenant_id('port', port_obj.get_uuid())
        perms2.owner = tenant_id
        ip_obj.set_perms2(perms2)
        # create instance
        ip_id = self._instance_ip_create(ip_obj)
        return ip_id
    # end _create_instance_ip

    def _port_create_instance_ip(self, net_obj, port_obj, port_q, ip_family=None):
        fixed_ips = port_q.get('fixed_ips')
        if fixed_ips is None:
            return

        # 1. find existing ips on port
        # 2. add new ips on port from update body
        # 3. delete old/stale ips on port
        stale_ip_ids = {}
        for iip in getattr(port_obj, 'instance_ip_back_refs', []):
            iip_obj = self._instance_ip_read(instance_ip_id=iip['uuid'])
            ip_addr = iip_obj.get_instance_ip_address()
            stale_ip_ids[ip_addr] = iip['uuid']

        created_iip_ids = []
        for fixed_ip in fixed_ips:
            try:
                ip_addr = fixed_ip.get('ip_address')
                if ip_addr is not None:
                    try:
                        # this ip survives to next gen
                        del stale_ip_ids[ip_addr]
                        continue
                    except KeyError:
                        pass

                    if (IPAddress(fixed_ip['ip_address']).version == 4):
                        ip_family="v4"
                    elif (IPAddress(fixed_ip['ip_address']).version == 6):
                        ip_family="v6"
                subnet_id = fixed_ip.get('subnet_id')
                ip_id = self._create_instance_ip(net_obj, port_obj, ip_addr,
                                                 subnet_id, ip_family)

                created_iip_ids.append(ip_id)
            except vnc_exc.HttpError as e:
                # Resources are not available
                for iip_id in created_iip_ids:
                    self._instance_ip_delete(instance_ip_id=iip_id)
                raise

        for stale_ip, stale_id in stale_ip_ids.items():
            self._instance_ip_delete(instance_ip_id=stale_id)
    # end _port_create_instance_ip

    # port api handlers
    @wait_for_api_server_connection
    def port_create(self, context, port_q):
        net_id = port_q['network_id']
        try:
            net_obj = self._network_read(net_id)
        except NoIdError:
            self._raise_contrail_exception('NetworkNotFound',
                                           net_id=net_id)
        tenant_id = self._get_tenant_id_for_create(context, port_q);
        proj_id = str(uuid.UUID(tenant_id))

        # if mac-address is specified, check against the exisitng ports
        # to see if there exists a port with the same mac-address
        if 'mac_address' in port_q:
            mac_dict = {"mac_address": [port_q['mac_address']]}
            filters = {'virtual_machine_interface_mac_addresses': json.dumps(mac_dict)}
            ports = self._virtual_machine_interface_list(back_ref_id=net_id,
                                                         filters=filters)
            if ports:
                raise self._raise_contrail_exception("MacAddressInUse",
                    net_id=net_id, mac=port_q['mac_address'])

        # initialize port object
        port_obj = self._port_neutron_to_vnc(port_q, net_obj, CREATE)

        # change owner
        perms2 = PermType2()
        perms2.owner = tenant_id
        port_obj.set_perms2(perms2)

        # always request for v4 and v6 ip object and handle the failure
        # create the object
        try:
            port_id = self._resource_create('virtual_machine_interface', port_obj)
        except BadRequest as e:
            msg = "Allowed address pairs are not allowed when port "\
                  "security is disabled"
            if msg == str(e):
                self._raise_contrail_exception(
                   'AddressPairAndPortSecurityRequired')
            else:
                self._raise_contrail_exception(
                   'BadRequest', resource='port', msg=str(e))

        # add support, nova boot --nic subnet-id=subnet_uuid
        subnet_id = port_q.get('subnet_id')
        if 'fixed_ips' in port_q:
            exception = None
            exception_kwargs = {}
            try:
                self._port_create_instance_ip(net_obj, port_obj, port_q)
            except RefsExistError as e:
                # failure in creating the instance ip. Roll back
                exception = 'Conflict'
                exception_kwargs = {'message': str(e)}
            except BadRequest as e:
                exception = 'BadRequest'
                exception_kwargs = {'resource': 'port', 'msg': str(e)}
            except vnc_exc.HttpError:
                # failure in creating the instance ip. Roll back
                exception = 'IpAddressGenerationFailure'
                exception_kwargs = {'net_id': net_obj.uuid}
            finally:
                if exception:
                    # failure in creating the instance ip. Roll back
                    self._virtual_machine_interface_delete(port_id=port_id)
                    self._raise_contrail_exception(exception, **exception_kwargs)
        elif net_obj.get_network_ipam_refs():
            ipv4_port_delete = False
            ipv6_port_delete = False
            try:
                self._port_create_instance_ip(net_obj, port_obj,
                     {'fixed_ips':[{'ip_address': None,
                                    'subnet_id':subnet_id}]},
                                              ip_family="v4")
            except BadRequest as e:
                ipv4_port_delete = True
            except vnc_exc.HttpError as e:
                # failure in creating the instance ip. Roll back
                self._virtual_machine_interface_delete(port_id=port_id)
                self._raise_contrail_exception('IpAddressGenerationFailure',
                                               resource='port', msg=str(e))

            try:
                self._port_create_instance_ip(net_obj, port_obj,
                     {'fixed_ips':[{'ip_address': None,
                                    'subnet_id':subnet_id}]},
                                              ip_family="v6")
            except BadRequest as e:
                ipv6_port_delete = True
            except vnc_exc.HttpError as e:
                # failure in creating the instance ip. Roll back
                self._virtual_machine_interface_delete(port_id=port_id)
                self._raise_contrail_exception('IpAddressGenerationFailure',
                                               resource='port', msg=str(e))

            # if if bad request is for both ipv4 and ipv6
            # delete the port and Roll back
            if ipv4_port_delete and ipv6_port_delete:
                    self._virtual_machine_interface_delete(port_id=port_id)
                    self._raise_contrail_exception('BadRequest',
                                                   resource='port', msg=str(e))

        # TODO below reads back default parent name, fix it
        port_obj = self._virtual_machine_interface_read(port_id=port_id)
        ret_port_q = self._port_vnc_to_neutron(port_obj)

        # create interface route table for the port if
        # subnet has a host route for this port ip.
        if self._apply_subnet_host_routes:
            self._port_check_and_add_iface_route_table(ret_port_q['fixed_ips'],
                                                       net_obj, port_obj)

        return ret_port_q
    # end port_create

    # TODO add obj param and let caller use below only as a converter
    @wait_for_api_server_connection
    def port_read(self, port_id):
        try:
            port_obj = self._virtual_machine_interface_read(port_id=port_id)
        except NoIdError:
            self._raise_contrail_exception('PortNotFound', port_id=port_id)

        ret_port_q = self._port_vnc_to_neutron(port_obj)

        return ret_port_q
    # end port_read

    @wait_for_api_server_connection
    def port_update(self, port_id, port_q):
        # if ip address passed then use it
        req_ip_addrs = []
        req_ip_subnets = []
        port_q['id'] = port_id
        port_obj = self._port_neutron_to_vnc(port_q, None, UPDATE)
        net_id = port_obj.get_virtual_network_refs()[0]['uuid']
        net_obj = self._network_read(net_id)
        try:
            self._resource_update('virtual_machine_interface', port_obj)
        except BadRequest as e:
            msg = "Allowed address pairs are not allowed when port "\
                  "security is disabled"
            if msg == str(e):
                self._raise_contrail_exception(
                   'AddressPairAndPortSecurityRequired')
            else:
                self._raise_contrail_exception(
                   'BadRequest', resource='port', msg=str(e))

        port_obj = self._virtual_machine_interface_read(port_id=port_id)
        ret_port_q = self._port_vnc_to_neutron(port_obj)

        return ret_port_q
    # end port_update

    @wait_for_api_server_connection
    def port_delete(self, port_id):
        port_obj = self._port_neutron_to_vnc({'id': port_id}, None, DELETE)
        if port_obj.parent_type == 'virtual-machine':
            instance_id = port_obj.parent_uuid
        else:
            vm_refs = port_obj.get_virtual_machine_refs()
            if vm_refs:
                instance_id = vm_refs[0]['uuid']
            else:
                instance_id = None

        if port_obj.get_logical_router_back_refs():
            self._raise_contrail_exception('L3PortInUse', port_id=port_id,
                device_owner=constants.DEVICE_OWNER_ROUTER_INTF)

        # release instance IP address
        iip_back_refs = getattr(port_obj, 'instance_ip_back_refs', None)
        if iip_back_refs:
            for iip_back_ref in iip_back_refs:
                try:
                    iip_obj = self._vnc_lib.instance_ip_read(
                        id=iip_back_ref['uuid'])

                    # in case of shared ip only delete the link to the VMI
                    iip_obj.del_virtual_machine_interface(port_obj)
                    if not iip_obj.get_virtual_machine_interface_refs():
                        try:
                            self._instance_ip_delete(
                                instance_ip_id=iip_back_ref['uuid'])
                        except RefsExistError:
                            self._instance_ip_update(iip_obj)
                    else:
                        self._instance_ip_update(iip_obj)
                except NoIdError:
                    # instance ip could be deleted by svc monitor if it is
                    # a shared ip. Ignore this error
                    continue

        # disassociate any floating IP used by instance
        fip_back_refs = getattr(port_obj, 'floating_ip_back_refs', None)
        if fip_back_refs:
            for fip_back_ref in fip_back_refs:
                self.floatingip_update(None, fip_back_ref['uuid'],
                                       {'port_id': None})

        tenant_id = self._get_obj_tenant_id('port', port_id)
        self._virtual_machine_interface_delete(port_id=port_id)

        # delete any interface route table associated with the port to handle
        # subnet host route Neutron extension, un-reference others
        for rt_ref in port_obj.get_interface_route_table_refs() or []:
            if _IFACE_ROUTE_TABLE_NAME_PREFIX_REGEX.match(rt_ref['to'][-1]):
                try:
                    self._resource_delete('interface_route_table',
                        rt_ref['uuid'])
                except (NoIdError, RefsExistError) as e:
                    pass

        # delete instance if this was the last port
        try:
            if instance_id:
                self._resource_delete('virtual_machine', instance_id)
        except (NoIdError, RefsExistError):
            pass

    # end port_delete

    def _port_fixed_ips_is_present(self, check, against):
        # filters = {'fixed_ips': {'ip_address': ['20.0.0.5', '20.0.0.6']}}
        # check = {'ip_address': ['20.0.0.5', '20.0.0.6']}
        # against = [{'subnet_id': 'uuid', 'ip_address': u'20.0.0.5'}]
        for addr in check['ip_address']:
            for item in against:
                if item['ip_address'] == addr:
                    return True

        return False
    # end _port_fixed_ips_is_present

    @wait_for_api_server_connection
    def port_list(self, context=None, filters=None):
        if not context:
            context = {'is_admin': True}

        if (filters.get('device_owner') == 'network:dhcp' or
            'network:dhcp' in filters.get('device_owner', [])):
             return []

        project_ids = self._validate_project_ids(context, filters)
        # normalize to dashed format uuid
        project_ids = [str(uuid.UUID(p)) for p in project_ids or []]

        port_objs = []
        if filters.get('device_id'):
            back_ref_ids = filters.get('device_id')
            port_objs_filtered_by_device_id = []
            founded_device_ids = set()
            for vmi_obj in self._virtual_machine_interface_list(
                    obj_uuids=filters.get('id'),
                    back_ref_id=back_ref_ids):
                for device_ref in (vmi_obj.get_virtual_machine_refs() or []) +\
                        (vmi_obj.get_logical_router_back_refs() or []):
                    # check if the device-id matches and if the network-id
                    # filter is set
                    if device_ref['uuid'] in filters.get('device_id') and \
                            filters.get('network_id'):
                        for vn_ref in vmi_obj.get_virtual_network_refs() or []:
                            # add only the vmi_obj that has also the same
                            # network-id
                            if vn_ref['uuid'] in filters.get('network_id'):
                                port_objs_filtered_by_device_id.append(vmi_obj)
                                founded_device_ids.add(device_ref['uuid'])
                    # without network-id filters
                    elif device_ref['uuid'] in filters.get('device_id'):
                        port_objs_filtered_by_device_id.append(vmi_obj)
                        founded_device_ids.add(device_ref['uuid'])

            # If some device ids not yet found look to router interfaces
            not_found_device_ids = set(filters.get('device_id')) -\
                founded_device_ids
            if not_found_device_ids:
                # Port has a back_ref to logical router, so need to read in
                # logical routers based on device ids
                router_objs = self._logical_router_list(
                    obj_uuids=list(not_found_device_ids),
                    parent_id=project_ids,
                    back_ref_id=filters.get('network_id'),
                    fields=['virtual_machine_interface_refs'])
                router_port_ids = [
                    vmi_ref['uuid']
                    for router_obj in router_objs
                    for vmi_ref in
                    router_obj.get_virtual_machine_interface_refs() or []
                ]
                # Add all router intefraces on private networks
                if router_port_ids:
                    port_objs.extend(self._virtual_machine_interface_list(
                        obj_uuids=router_port_ids, parent_id=project_ids))

                # Add router gateway interface
                for router in router_objs:
                    gw_vmi = self._get_router_gw_interface_for_neutron(context,
                                                                       router)
                    if gw_vmi is not None:
                        port_objs.append(gw_vmi)

            # Filter it with project ids if there are.
            if project_ids:
                port_objs.extend([p for p in port_objs_filtered_by_device_id
                                  if p.parent_uuid in project_ids])
            else:
                port_objs.extend(port_objs_filtered_by_device_id)
        elif filters.get('network_id'):
            port_objs = self._virtual_machine_interface_list(
                obj_uuids=filters.get('id'),
                back_ref_id=filters.get('network_id'))
        else:
            port_objs = self._virtual_machine_interface_list(
                obj_uuids=filters.get('id'),
                parent_id=project_ids)

        neutron_ports = self._port_list(port_objs)

        ret_list = []
        for neutron_port in neutron_ports:
            if not self._filters_is_present(filters, 'name',
                                            neutron_port['name']):
                continue
            if not self._filters_is_present(filters, 'device_owner',
                                            neutron_port["device_owner"]):
                continue
            if ('fixed_ips' in filters and
                not self._port_fixed_ips_is_present(filters['fixed_ips'],
                                                    neutron_port['fixed_ips'])):
                continue
            if not self._filters_is_present(filters, 'mac_address',
                                            neutron_port['mac_address']):
                continue
            if not self._filters_is_present(filters, 'status',
                                            neutron_port['status']):
                continue

            ret_list.append(neutron_port)
        return ret_list

    @wait_for_api_server_connection
    def port_count(self, filters=None):
        count = self._resource_count_optimized("virtual_machine_interfaces",
                                               filters)
        if count is not None:
            return count

        if (filters.get('device_owner') == 'network:dhcp' or
            'network:dhcp' in filters.get('device_owner', [])):
            return 0

        if 'tenant_id' in filters:
            if isinstance(filters['tenant_id'], list):
                project_id = str(uuid.UUID(filters['tenant_id'][0]))
            else:
                project_id = str(uuid.UUID(filters['tenant_id']))

            nports = self._port_list_project(project_id, count=True)
        else:
            # across all projects - TODO very expensive,
            # get only a count from api-server!
            nports = len(self.port_list(filters=filters))

        return nports
    #end port_count

    def populate_default_rule(self, ethertype = 'IPv4'):
        def_rule = {}
        def_rule['port_range_min'] = 0
        def_rule['port_range_max'] = 65535
        def_rule['direction'] = 'egress'
        def_rule['remote_group_id'] = None
        def_rule['protocol'] = 'any'

        if ethertype == 'IPv4':
            def_rule['ethertype'] = 'IPv4'
            def_rule['remote_ip_prefix'] = '0.0.0.0/0'
        else:
            def_rule['ethertype'] = 'IPv6'
            def_rule['remote_ip_prefix'] = '::/0'

        return def_rule

    # security group api handlers
    @wait_for_api_server_connection
    def security_group_create(self, sg_q):
        sg_obj = self._security_group_neutron_to_vnc(sg_q, CREATE)

        # ensure default SG and deny create if the group name is default
        if sg_q['name'] == 'default':
            self._ensure_default_security_group_exists(sg_q['tenant_id'])
            self._raise_contrail_exception("SecurityGroupAlreadyExists")

        sg_uuid = self._resource_create('security_group', sg_obj)

        #allow all IPv4 egress traffic
        def_rule = self.populate_default_rule('IPv4')
        rule = self._security_group_rule_neutron_to_vnc(def_rule, CREATE)
        self._security_group_rule_create(sg_uuid, rule)

        #allow all IPv6 egress traffic
        def_rule = self.populate_default_rule('IPv6')
        rule = self._security_group_rule_neutron_to_vnc(def_rule, CREATE)
        self._security_group_rule_create(sg_uuid, rule)

        #read the latest sg_obj from db to get associated rules
        #security_group_read will internally call _security_group_vnc_to_neutron
        ret_sg_q = self.security_group_read(sg_uuid)
        return ret_sg_q
    #end security_group_create

    @wait_for_api_server_connection
    def security_group_update(self, sg_id, sg_q):
        sg_q['id'] = sg_id
        sg_obj = self._security_group_neutron_to_vnc(sg_q, UPDATE)
        self._resource_update('security_group', sg_obj)

        ret_sg_q = self._security_group_vnc_to_neutron(sg_obj)

        return ret_sg_q
    #end security_group_update

    @wait_for_api_server_connection
    def security_group_read(self, sg_id):
        try:
            sg_obj = self._vnc_lib.security_group_read(id=sg_id)
        except NoIdError:
            self._raise_contrail_exception('SecurityGroupNotFound', id=sg_id)

        return self._security_group_vnc_to_neutron(sg_obj)
    #end security_group_read

    @wait_for_api_server_connection
    def security_group_delete(self, context, sg_id):
        try:
            sg_obj = self._vnc_lib.security_group_read(id=sg_id)
            if (sg_obj.name == 'default' and
                str(uuid.UUID(context['tenant_id'])) == sg_obj.parent_uuid):
                # Deny delete if the security group name is default and
                # the owner of the SG is deleting it.
                self._raise_contrail_exception(
                    'SecurityGroupCannotRemoveDefault')
        except NoIdError:
            return

        try:
            self._resource_delete('security_group', sg_id)
        except RefsExistError:
            self._raise_contrail_exception('SecurityGroupInUse', id=sg_id)

        # Once the security group is deleted, delete the zk node
        self._zookeeper_client.delete_node(
            '%s/%s' % (
                self.security_group_lock_prefix, sg_id
            ))

   #end security_group_delete

    @wait_for_api_server_connection
    def security_group_list(self, context, filters=None):
        ret_list = []
        memo_req = {}

        # collect phase
        self._ensure_default_security_group_exists(context['tenant_id'])

        if filters and 'id' in filters:
            all_sgs = self._vnc_lib.security_groups_list(
                obj_uuids=filters['id'], detail=True)
        elif context and not context['is_admin']:
            all_sgs = self._security_group_list_project(
                str(uuid.UUID(context['tenant'])), filters)
        else:  # admin context
            if filters and 'tenant_id' in filters:
                all_sgs = []
                for p_id in self._validate_project_ids(context, filters) or []:
                    sgs = self._security_group_list_project(p_id, filters)
                    all_sgs.extend(sgs)
            else:  # no tenant_id filter
                all_sgs = self._security_group_list_project(None, filters)

        memo_req['security_groups'] = dict(
            (sg_obj.get_fq_name_str(), sg_obj.uuid) for sg_obj in all_sgs)
        # prune phase
        for sg_obj in all_sgs:
            if sg_obj.get_fq_name() == SG_NO_RULE_FQ_NAME:
                continue
            name = sg_obj.get_display_name() or sg_obj.name
            if not self._filters_is_present(filters, 'name', name):
                continue
            sg_info = self._security_group_vnc_to_neutron(sg_obj, memo_req,
                                                          oper=LIST)
            if sg_info is None:
                continue
            ret_list.append(sg_info)

        return ret_list
    #end security_group_list

    def _validate_port_range(self, rule):
        """Check that port_range is valid."""
        if (rule['port_range_min'] is None and
            rule['port_range_max'] is None):
            return
        if not rule['protocol']:
            self._raise_contrail_exception(
                'SecurityGroupProtocolRequiredWithPorts')
        if rule['protocol'] in [constants.PROTO_NAME_TCP,
                                constants.PROTO_NAME_UDP,
                                str(constants.PROTO_NUM_TCP),
                                str(constants.PROTO_NUM_UDP)]:
            if (rule['port_range_min'] is not None and
                rule['port_range_min'] <= rule['port_range_max']):
                pass
            else:
                self._raise_contrail_exception('SecurityGroupInvalidPortRange')
        elif rule['protocol'] in [constants.PROTO_NAME_ICMP,
                                  str(constants.PROTO_NUM_ICMP)]:
            for attr, field in [('port_range_min', 'type'),
                                ('port_range_max', 'code')]:
                if rule[attr] > 255:
                    self._raise_contrail_exception(
                        'SecurityGroupInvalidIcmpValue', field=field,
                        attr=attr, value=rule[attr])
            if (rule['port_range_min'] is None and
                    rule['port_range_max']):
                self._raise_contrail_exception('SecurityGroupMissingIcmpType',
                                               value=rule['port_range_max'])

    @wait_for_api_server_connection
    def security_group_rule_create(self, sgr_q):
        self._validate_port_range(sgr_q)
        sg_id = sgr_q['security_group_id']
        sg_rule = self._security_group_rule_neutron_to_vnc(sgr_q, CREATE)
        self._security_group_rule_create(sg_id, sg_rule)
        ret_sg_rule_q = self._security_group_rule_vnc_to_neutron(sg_id,
                                                                 sg_rule)

        return ret_sg_rule_q
    #end security_group_rule_create

    @wait_for_api_server_connection
    def security_group_rule_read(self, context, sgr_id):
        project_uuid = None
        if not context['is_admin']:
            project_uuid = str(uuid.UUID(context['tenant_id']))

        sg_obj, sg_rule = self._security_group_rule_find(sgr_id, project_uuid)
        if sg_obj and sg_rule:
            sgr_info = self._security_group_rule_vnc_to_neutron(sg_obj.uuid,
                                                                sg_rule,
                                                                sg_obj)
            if sgr_info:
                return sgr_info
        self._raise_contrail_exception('SecurityGroupRuleNotFound', id=sgr_id)
    # end security_group_rule_read

    @wait_for_api_server_connection
    def security_group_rule_delete(self, context, sgr_id):
        project_uuid = None
        if not context['is_admin']:
            project_uuid = str(uuid.UUID(context['tenant_id']))

        sg_obj, sg_rule = self._security_group_rule_find(sgr_id, project_uuid)
        if sg_obj and sg_rule:
            return self._security_group_rule_delete(sg_obj, sg_rule)

        self._raise_contrail_exception('SecurityGroupRuleNotFound', id=sgr_id)
    # end security_group_rule_delete

    @wait_for_api_server_connection
    def security_group_rules_read(self, sg_id, sg_obj=None, memo_req=None,
                                  oper=READ):
        try:
            if not sg_obj:
                sg_obj = self._vnc_lib.security_group_read(id=sg_id)

            sgr_entries = sg_obj.get_security_group_entries()
            sg_rules = []
            if sgr_entries is None:
                return

            for sg_rule in sgr_entries.get_policy_rule():
                sgr_info = self._security_group_rule_vnc_to_neutron(
                    sg_obj.uuid, sg_rule, sg_obj, memo_req, oper=oper)
                if sgr_info:
                    sg_rules.append(sgr_info)
        except NoIdError:
            self._raise_contrail_exception('SecurityGroupNotFound', id=sg_id)

        return sg_rules
    # end security_group_rules_read

    @wait_for_api_server_connection
    def security_group_rule_list(self, context=None, filters=None):
        ret_list = []

        # collect phase
        all_sgs = []
        if filters and 'tenant_id' in filters:
            for p_id in self._validate_project_ids(context, filters) or []:
                project_sgs = self._security_group_list_project(p_id, filters)
                all_sgs.append(project_sgs)
        else:  # no filters
            p_id = None
            if context and not context['is_admin']:
                p_id = str(uuid.UUID(context['tenant']))
            all_sgs.append(self._security_group_list_project(p_id, filters))

        # prune phase
        for project_sgs in all_sgs:
            for sg_obj in project_sgs:
                sgr_info = self.security_group_rules_read(sg_obj.uuid, sg_obj,
                                                          oper=LIST)
                if sgr_info is None:
                    continue
                ret_list.extend(sgr_info)

        return ret_list
    # end security_group_rule_list

    # route table api handlers
    @wait_for_api_server_connection
    def route_table_create(self, rt_q):
        rt_obj = self._route_table_neutron_to_vnc(rt_q, CREATE)
        try:
            rt_uuid = self._route_table_create(rt_obj)
        except RefsExistError as e:
            self._raise_contrail_exception('BadRequest',
                resource='route_table', msg=str(e))
        ret_rt_q = self._route_table_vnc_to_neutron(rt_obj)
        return ret_rt_q
    #end route_table_create

    @wait_for_api_server_connection
    def route_table_read(self, rt_id, oper=READ):
        try:
            rt_obj = self._vnc_lib.route_table_read(id=rt_id)
        except NoIdError:
            # TODO add route table specific exception
            self._raise_contrail_exception('NetworkNotFound', net_id=rt_id)

        return self._route_table_vnc_to_neutron(rt_obj, oper=oper)
    #end route_table_read

    @wait_for_api_server_connection
    def route_table_update(self, rt_id, rt_q):
        rt_q['id'] = rt_id
        rt_obj = self._route_table_neutron_to_vnc(rt_q, UPDATE)
        self._resource_update('route_table', rt_obj)
        return self._route_table_vnc_to_neutron(rt_obj)
    #end policy_update

    @wait_for_api_server_connection
    def route_table_delete(self, rt_id):
        self._route_table_delete(rt_id)
    #end route_table_delete

    @wait_for_api_server_connection
    def route_table_list(self, context, filters=None):
        ret_list = []

        # collect phase
        all_rts = []  # all rts in all projects
        if filters and 'tenant_id' in filters:
            for p_id in self._validate_project_ids(context, filters) or []:
                project_rts = self._route_table_list_project(p_id)
                all_rts.append(project_rts)
        elif filters and 'name' in filters:
            p_id = str(uuid.UUID(context['tenant']))
            project_rts = self._route_table_list_project(p_id)
            all_rts.append(project_rts)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_rts = self._route_table_list_project(proj_id)
                all_rts.append(project_rts)

        # prune phase
        for project_rts in all_rts:
            for proj_rt in project_rts:
                proj_rt_id = proj_rt['uuid']
                if not self._filters_is_present(filters, 'id', proj_rt_id):
                    continue
                rt_info = self.route_table_read(proj_rt_id, oper=LIST)
                if rt_info is None:
                    continue
                if not self._filters_is_present(filters, 'name',
                                                rt_info['name']):
                    continue
                ret_list.append(rt_info)

        return ret_list
    #end route_table_list

    #service instance api handlers
    @wait_for_api_server_connection
    def svc_instance_create(self, si_q):
        si_obj = self._svc_instance_neutron_to_vnc(si_q, CREATE)
        si_uuid = self._svc_instance_create(si_obj)
        ret_si_q = self._svc_instance_vnc_to_neutron(si_obj)
        return ret_si_q
    #end svc_instance_create

    @wait_for_api_server_connection
    def svc_instance_read(self, si_id, oper=READ):
        try:
            si_obj = self._vnc_lib.service_instance_read(id=si_id)
        except NoIdError:
            # TODO add svc instance specific exception
            self._raise_contrail_exception('NetworkNotFound', net_id=si_id)

        return self._svc_instance_vnc_to_neutron(si_obj, oper=oper)
    #end svc_instance_read

    @wait_for_api_server_connection
    def svc_instance_delete(self, si_id):
        self._svc_instance_delete(si_id)
    #end svc_instance_delete

    @wait_for_api_server_connection
    def svc_instance_list(self, context, filters=None):
        ret_list = []

        # collect phase
        all_sis = []  # all sis in all projects
        if filters and 'tenant_id' in filters:
            for p_id in self._validate_project_ids(context, filters) or []:
                project_sis = self._svc_instance_list_project(p_id)
                all_sis.append(project_sis)
        elif filters and 'name' in filters:
            p_id = str(uuid.UUID(context['tenant']))
            project_sis = self._svc_instance_list_project(p_id)
            all_sis.append(project_sis)
        else:  # no filters
            dom_projects = self._project_list_domain(None)
            for project in dom_projects:
                proj_id = project['uuid']
                project_sis = self._svc_instance_list_project(proj_id)
                all_sis.append(project_sis)

        # prune phase
        for project_sis in all_sis:
            for proj_si in project_sis:
                proj_si_id = proj_si['uuid']
                if not self._filters_is_present(filters, 'id', proj_si_id):
                    continue
                si_info = self.svc_instance_read(proj_si_id, oper=LIST)
                if si_info is None:
                    continue
                if not self._filters_is_present(filters, 'name',
                                                si_info['name']):
                    continue
                ret_list.append(si_info)

        return ret_list
    #end svc_instance_list

    @wait_for_api_server_connection
    def virtual_router_read(self, vrouter_id):
        try:
            vrouter_obj = self._vnc_lib.virtual_router_read(fq_name=vrouter_id)
        except NoIdError:
            # TODO add VirtualRouter specific exception
            self._raise_contrail_exception('VirtualRouterNotFound',
                                           vrouter_id=vrouter_id)

        return self._virtual_router_to_neutron(vrouter_obj)
    #end virtual_router_read

    """Firewall as a Service v2 Contrail mapping

        Neutron FWaaS project: https://docs.openstack.org/neutron/latest/admin/fwaas.html#fwaas-v2
        Contrail mapping spec: https://github.com/Juniper/contrail-specs/blob/master/neutron_FWaaSv2.md
    """

    def _compute_firewall_group_status(self, firewall_group):
        """Compute a status of specified Firewall Group

        Validates 'ACTIVE', 'DOWN', 'INACTIVE', 'ERROR' and None as follows:
            - "ACTIVE"   : admin_state_up is True and exists ports and policies
            - "INACTIVE" : admin_state_up is True and with no ports or no
                           policy
            - "DOWN"     : admin_state_up is False

        :params firewall_group: Firewall Group dictionary from the status is
            computed
        :returns: Neutron status
        """
        if not firewall_group['admin_state_up']:
            return constants.DOWN

        if (firewall_group['ports'] and (
                firewall_group.get('ingress_firewall_policy_id') or
                firewall_group.get('egress_firewall_policy_id'))):
            return constants.ACTIVE

        return constants.INACTIVE

    def _firewall_group_neutron_to_vnc(self, context, firewall_group, id=None):
        """Neutron Firewall Group resource to Contrail Application Policy Set

        Convert Neutron Firewall Group resource to Contrail Application Policy
        Set.

        ==========================  ===============================
        Neutron Firewall Group      Contrail Application Policy Set
        ==========================  ===============================
        id                          `uuid`
        tenant_id                   `project` parent reference
        name                        `display_name`
        description                 `id_perms.description`
        admin_state_up              `id_perms.enable`
        status                      see _compute_firewall_group_status method
        share                       `perms2`
        ports                       `virtual-machine-interface` references
        ingress_firewall_policy_id  `firewall-policy` reference
        egress_firewall_policy_id   equals to `ingress_firewall_policy_id`,
                                    Contrail does not distinguish ingress or
                                    egress flow for network policy and firewall
                                    policy (only for security group)
        ==========================  ===============================

        :param context: Neutron api request context
        :param firewall_group: Firewall Group dictionary to convert
        :param id: permits to read APS if method called during an update
            request. If `None`, instantiate new APS
        :returns: vnc_api.gen.resource_client.ApplicationPolicySet
        """

        if not id:  # creation
            # cannot use default firewall group name
            if (firewall_group.get('name') ==
                    _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME):
                vnc_openstack.ensure_default_firewall_group(
                    self._vnc_lib, firewall_group['tenant_id'])
                self._raise_contrail_exception(
                    "FirewallGroupDefaultAlreadyExists")
            project = self._get_project_obj(firewall_group)
            project_uuid = project.uuid
            aps = ApplicationPolicySet(
                name=firewall_group.get('name') or str(uuid.uuid4()),
                parent_obj=project,
                perms2=PermType2(owner=project.uuid),
            )
        else:  # update
            try:
                aps = self._vnc_lib.application_policy_set_read(id=id)
            except NoIdError:
                self._raise_contrail_exception('FirewallGroupNotFound',
                                               firewall_id=id)
            project_uuid = aps.parent_uuid

            # limit default firewall group update
            if aps.name == _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME:
                if context['is_admin']:
                    attrs = set(['name'])
                else:
                    attrs = set(['name', 'description', 'admin_state_up',
                                'ingress_firewall_policy_id',
                                'egress_firewall_policy_id'])
                if set(firewall_group.keys()) & attrs:
                    self._raise_contrail_exception(
                        "FirewallGroupCannotUpdateDefault")

            # cannot use default firewall group name
            if (firewall_group.get('name') ==
                    _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME):
                vnc_openstack.ensure_default_firewall_group(
                    self._vnc_lib, firewall_group['tenant_id'])
                self._raise_contrail_exception(
                    "FirewallGroupDefaultAlreadyExists")

        if 'name' in firewall_group:
            aps.set_display_name(firewall_group['name'])

        if set(['admin_state_up', 'description']) & set(firewall_group.keys()):
            id_perms = aps.get_id_perms() or IdPermsType(enable=True)
            if 'admin_state_up' in firewall_group:
                id_perms.set_enable(firewall_group['admin_state_up'])
            if 'description' in firewall_group:
                id_perms.set_description(firewall_group['description'])
            aps.set_id_perms(id_perms)

        if firewall_group.get('shared'):
            perms2 = aps.get_perms2() or PermType2(owner=project_uuid)
            perms2.set_global_access(PERMS_RWX)
            aps.set_perms2(perms2)

        # Add refs to the Firewall Policy
        # if one of the policy ref (ingress or egress) is set, use it, if both
        # set, raise a Not impmented error which explain Contrail firewall
        # limitation
        attr_not_specified = object()
        ingress = firewall_group.get(
            'ingress_firewall_policy_id', attr_not_specified)
        egress = firewall_group.get(
            'egress_firewall_policy_id', attr_not_specified)
        if ingress != attr_not_specified or egress != attr_not_specified:
            if (ingress not in [None, attr_not_specified] and
                    egress not in [None, attr_not_specified] and
                    ingress != egress):
                msg =("Contrail firewall policies does not distinguish "
                      "ingress and egress flow. Please specify same policy in "
                      "ingress or egress attributes (if only one is set, the "
                      "opposite will be automatically set with the same "
                      "value)")
                self._raise_contrail_exception(
                    'BadRequest', resource='firewall_group', msg=msg)
            fp_id = ingress
            if ingress in [None, attr_not_specified]:
                fp_id = egress
            if fp_id not in [None, attr_not_specified]:
                fp_fq_name = self._vnc_lib.id_to_fq_name(fp_id)
                aps.set_firewall_policy_list(
                    [fp_fq_name], [FirewallSequence(sequence='0.0')])
            else:
                aps.set_firewall_policy_list([], [])

        return aps

    def _application_policy_set_vnc_to_neutron(self, aps, fields=None):
        """Convert Contrail Application Policy Set to Neutron Firewall Group

        :param aps: vnc_api.gen.resource_client.ApplicationPolicySet instance
            to convert
        :param fields: a list of strings that are valid keys in a Firewall
            Group dictionary. Only these fields will be returned.
        :retruns: Firewall Group dictionary
        """
        firewall_group = {
            'id': aps.uuid,
            'name': aps.display_name,
            'description': aps.get_id_perms().get_description(),
            'admin_state_up': aps.get_id_perms().get_enable(),
            'project_id': aps.get_perms2().get_owner().replace('-', ''),
            'tenant_id': aps.get_perms2().get_owner().replace('-', ''),
            'shared': self._is_shared(aps),
        }

        # retrieve associated VMI from the dedicated tag
        tag_fq_name = aps.get_fq_name()[:-1] + [
            '%s=%s' % (_NEUTRON_FWAAS_TAG_TYPE, aps.uuid)]
        for tag_ref in aps.get_tag_refs() or []:
            if tag_ref['to'] == tag_fq_name:
                aps_tag = self._vnc_lib.tag_read(
                    id=tag_ref['uuid'],
                    fields=['virtual_machine_interface_back_refs'])
                firewall_group['ports'] = [
                    vmi['uuid'] for vmi in
                    aps_tag.get_virtual_machine_interface_back_refs() or []]
                break
        else:
            # if APS does not have ref to an neutron firewall tag type with
            # value equals <aps UUID>, that means that APS does not correspond
            # to a Neutron firwall group, ignore it
            return

        # locate firewall policy (only one) and set it as ingress and egress
        # policy
        sorted_fp_refs = fp_refs = aps.get_firewall_policy_refs() or []
        if len(fp_refs) > 1:
            msg = ("Application Policy Set %s(%s) corresponding to a Neutron "
                   "Firewall Group have more that on Firewall Policy "
                   "reference, keep only one with lowest sequence number" %
                   (aps.get_fq_name_str(), aps.uuid))
            self.logger.warning(msg)
            sorted_fp_refs = sorted(
                fp_refs, key=lambda ref: float(ref['attr'].sequence))
            for fp_ref in sorted_fp_refs[1:]:
                self._vnc_lib.ref_update(
                    'application_policy_set',
                    aps.uuid,
                    'firewall_policy',
                    fp_ref['uuid'],
                    fp_ref['to'],
                    'DELETE')
        if sorted_fp_refs:
            firewall_group['ingress_firewall_policy_id'] =\
                sorted_fp_refs[0]['uuid']
            firewall_group['egress_firewall_policy_id'] =\
                sorted_fp_refs[0]['uuid']

        firewall_group['status'] = self._compute_firewall_group_status(
            firewall_group)

        return filter_fields(firewall_group, fields)

    def _apply_firewall_group_to_associated_port(self, aps, firewall_group):
        """Add or remove reference(s) from VMI to dedicated application Tag

        :param aps: vnc_api.gen.resource_client.ApplicationPolicySet instance
        :param firewall_group: Firewall Group dictionary
        """
        if 'ports' not in firewall_group:
            return
        new_ports = set(firewall_group['ports'])

        tag = self._vnc_lib.tag_read(
            aps.get_tag_refs()[0]['to'],
            fields=['virtual_machine_interface_back_refs'])
        old_ports = {
            ref['uuid']
            for ref in tag.get_virtual_machine_interface_back_refs() or []}

        for port_id in old_ports - new_ports:
            self._vnc_lib.set_tags(
                FakeVncLibResource('virtual_machine_interface', port_id),
                {_NEUTRON_FWAAS_TAG_TYPE: {'delete_values': [aps.uuid]}})

        for port_id in new_ports - old_ports:
            self._vnc_lib.set_tag(
                FakeVncLibResource('virtual_machine_interface', port_id),
                _NEUTRON_FWAAS_TAG_TYPE, aps.uuid)

    def firewall_group_create(self, context, firewall_group):
        """Create Firewall Group

        Maps a Neutron Firewall Group resource to a Contrail Application
        Policy. The APS is a child of the project and it is owned by that
        project. For each Neutron Firewall Group a Contrail neutron_fwaas Tag
        is dedicated (with value `<APS UUID>`) which is used to apply policies
        on the port.

        Contrail does not distinguish ingress or egress flow for network policy
        and firewall policy (it does only for security group) so the FWaaS
        implementation is limited and does not allow to distinguish ingress or
        egress policies. Ingress and egress policies of a Firewall Group is
        always the same if one set.

        :param context: Neutron api request context
        :param firewall_group: dictionary describing the Firewall Group
        :returns: Firewall Group dictionary populated
        """
        project = self._get_project_obj(firewall_group)
        aps = self._firewall_group_neutron_to_vnc(context, firewall_group)

        # Create Application Policy Set
        try:
            id = self._resource_create('application_policy_set', aps)
        except BadRequest as e:
            self._raise_contrail_exception(
                'BadRequest', resource='firewall_group', msg=str(e))
        get_context().push_undo(
            self._vnc_lib.application_policy_set_delete, id=id)

        # Create dedicated Tag and references it from the APS
        tag = Tag(
            tag_type_name=_NEUTRON_FWAAS_TAG_TYPE,
            tag_value=id,
            parent_obj=project)
        try:
            self._vnc_lib.tag_create(tag)
        except RefsExistError:
            pass
        get_context().push_undo(self._vnc_lib.tag_delete, id=tag.uuid)

        self._vnc_lib.set_tag(aps, tag.tag_type_name, tag.tag_value)

        aps = self._vnc_lib.application_policy_set_read(id=id)

        # Add ref to all ports associated with the firewall group
        self._apply_firewall_group_to_associated_port(aps, firewall_group)

        return self._application_policy_set_vnc_to_neutron(aps)

    def firewall_group_read(self, context, id, fields=None):
        """Retrieve Firewall Group

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Group to fetch
        :param fields: a list of strings that are valid keys in a Firewall
            Group dictionary. Only these fields will be returned.
        :returns: Firewall Group dictionary
        """
        try:
            aps = self._vnc_lib.application_policy_set_read(id=id)
        except NoIdError:
            self._raise_contrail_exception('FirewallGroupNotFound',
                                           firewall_id=id)

        firewall_group = self._application_policy_set_vnc_to_neutron(
            aps, fields)
        if not firewall_group:
            self._raise_contrail_exception('FirewallGroupNotFound',
                                           firewall_id=id)

        return firewall_group

    def firewall_group_list(self, context, filters=None, fields=None):
        """Retrieve a list of Firewall Group

        :param context: Neutron api request context
        :param filters: a dictionary with keys that are valid keys for a
            Firewall Group
        :param fields: a list of strings that are valid keys in a Firewall
            Group dictionary. Only these fields will be returned.
        :returns: Firewall Group dictionary
        """
        for project_id in self._validate_project_ids(context, filters) or []:
            vnc_openstack.ensure_default_firewall_group(self._vnc_lib,
                                                        project_id)

        results = []
        if not filters:
            filters = {}
        if 'name' in filters:
            filters['display_name'] = filters.pop('name')
        shared = filters.pop('shared', [False])[0]
        parent_ids = self._validate_project_ids(context, filters)
        filters.pop('tenant_id', None)
        filters.pop('project_id', None)
        apss = self._vnc_lib.application_policy_sets_list(
            detail=True,
            shared=shared,
            parent_id=parent_ids,
            obj_uuids=filters.pop('id', None),
            back_ref_id=(
                filters.pop('ingress_firewall_policy_id', []) +
                filters.pop('egress_firewall_policy_id', [])) or None,
            filters=filters)
        for aps in apss:
            if (shared and aps.get_perms2().owner.replace('-', '') ==
                    context['tenant_id']):
                continue
            firewall_group = self._application_policy_set_vnc_to_neutron(
                aps, fields)
            if not firewall_group:
                continue
            if ('ports' in filters and
                    not set(filters['ports']) &
                    set(firewall_group.get('ports', []))):
                continue
            results.append(firewall_group)

        return results

    def firewall_group_update(self, context, id, firewall_group):
        """Update value of Firewall Group

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Group to update
        :param firewall_group: dictionary with keys indicating fields to
            update
        :returns: Firewall Group dictionary updated
        """
        aps = self._firewall_group_neutron_to_vnc(context, firewall_group, id)
        # Update ref to all ports associated with the firewall group
        self._apply_firewall_group_to_associated_port(aps, firewall_group)

        self._resource_update('application_policy_set', aps)

        return self._application_policy_set_vnc_to_neutron(
            self._vnc_lib.application_policy_set_read(id=id))

    def firewall_group_delete(self, context, id):
        """Delete Firewall Group

        Delete corresponding Contrail APS and dedicated application Tag.

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Group to delete
        """
        firewall_group = self.firewall_group_read(
            context, id, fields=['name', 'ports'])

        # only admin can delete default firewall group
        if (not context['is_admin'] and
                (firewall_group['name'] ==
                 _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME)):
            self._raise_contrail_exception('FirewallGroupCannotRemoveDefault')

        # Should not occur as neutron fwaas code prevents to delete a firewall
        # group in active state (active means firewall group have policy+ports)
        for port_id in firewall_group['ports']:
            self._vnc_lib.unset_tag(
                FakeVncLibResource('virtual_machine_interface', port_id),
                _NEUTRON_FWAAS_TAG_TYPE)
        self._vnc_lib.unset_tag(
            FakeVncLibResource('application_policy_set', id),
            _NEUTRON_FWAAS_TAG_TYPE)
        tag_name = '%s=%s' % (_NEUTRON_FWAAS_TAG_TYPE, id)
        tag_fq_name = self._vnc_lib.id_to_fq_name(id)[:-1] + [tag_name]
        try:
            self._vnc_lib.tag_delete(fq_name=tag_fq_name)
        except RefsExistError:
            # delete any firewall rule ref created when a remote firewall group
            # was definied in the rule
            tag = self._vnc_lib.tag_read(tag_fq_name,
                                         fields=['firewall_rule_back_refs'])
            ep_types = ['endpoint_1', 'endpoint_2']
            for fr_ref in tag.get_firewall_rule_back_refs() or []:
                fr = self._vnc_lib.firewall_rule_read(id=fr_ref['uuid'],
                                                      fields=ep_types)
                for ep_type in ep_types:
                    ep = getattr(fr, 'get_%s' % ep_type)()
                    if (not ep or not ep.get_tags() or
                            tag_name not in ep.get_tags()):
                        continue
                    tags = ep.get_tags()
                    tags.remove(tag_name)
                    ep.set_tags(tags)
                    getattr(fr, 'set_%s' % ep_type)(ep)
                self._vnc_lib.firewall_rule_update(fr)
            self._vnc_lib.tag_delete(fq_name=tag_fq_name)
        self._vnc_lib.application_policy_set_delete(id=id)

    def _firewall_policy_neutron_to_vnc(self, firewall_policy, id=None):
        """Neutron Firewall Policy resource to Contrail Firewall Policy

        =======================  ========================
        Neutron Firewall Policy  Contrail Firewall Policy
        =======================  ========================
        id                       `uuid`
        tenant_id                `project` parent reference and owner
        name                     `display_name`
        description              `id_perms.description`
        share                    `perms2`
        firewall_rules           `firewall-rule` references with a sequence
                                   number for ordering
        audited                  `audited` (Not yet implemented, for the moment
                                 it is mapped to `id_perms.enable`)
        =======================  ========================

        :param firewall_policy: Firewall Policy dictionary to convert
        :param id: permits to read FP if method called during an update
            request. If `None`, instantiate new FP
        :returns: vnc_api.gen.resource_client.FirewallPolicy,
        """
        # cannot use default firewall policy name
        if (firewall_policy.get('name') ==
                _NEUTRON_FIREWALL_DEFAULT_GROUP_POLICY_NAME):
            vnc_openstack.ensure_default_firewall_group(
                self._vnc_lib, firewall_policy['tenant_id'])
            msg = ("Default firewall policy already exists. 'default' is the "
                   "reserved name for firewall policy.")
            self._raise_contrail_exception(
                'BadRequest', resource='firewall_policy', msg=msg)

        if not id:  # creation
            project = self._get_project_obj(firewall_policy)
            project_uuid = project.uuid
            fp = FirewallPolicy(
                name=firewall_policy.get('name') or str(uuid.uuid4()),
                parent_obj=project,
                id_perms=IdPermsType(enable=False),
                perms2=PermType2(owner=project.uuid),
            )
        else:  # update
            try:
                fp = self._vnc_lib.firewall_policy_read(id=id)
            except NoIdError:
                self._raise_contrail_exception('FirewallPolicyNotFound',
                                               firewall_policy_id=id)
            project_uuid = fp.parent_uuid

        if 'name' in firewall_policy:
            fp.set_display_name(firewall_policy['name'])

        if set(['audited', 'description']) & set(firewall_policy.keys()):
            id_perms = fp.get_id_perms() or IdPermsType(enable=False)
            if 'audited' in firewall_policy:
                id_perms.set_enable(firewall_policy['audited'])
            if 'description' in firewall_policy:
                id_perms.set_description(firewall_policy['description'])
            fp.set_id_perms(id_perms)

        if firewall_policy.get('shared'):
            perms2 = fp.get_perms2() or PermType2(owner=project_uuid)
            perms2.set_global_access(PERMS_RWX)
            fp.set_perms2(perms2)

        if 'firewall_rules' in firewall_policy:
            fr_fq_names = []
            fr_attrs = []
            for idx, fr_id in enumerate(firewall_policy['firewall_rules']):
                fr_fq_names.append(self._vnc_lib.id_to_fq_name(fr_id))
                fr_attrs.append(FirewallSequence(sequence='%0.1f' % idx))
            fp.set_firewall_rule_list(fr_fq_names, fr_attrs)

        return fp

    def _firewall_policy_vnc_to_neutron(self, fp, fields=None):
        """Convert Contrail Firewall Policy to Neutron Firewall Policy

        :param fp: vnc_api.gen.resource_client.FirewallPolicy instance to
            convert
        :param fields: a list of strings that are valid keys in a Firewall
            Policy dictionary. Only these fields will be returned.
        :retruns: Firewall Policy dictionary
        """
        firewall_policy = {
            'id': fp.uuid,
            'name': fp.display_name,
            'description': fp.get_id_perms().get_description(),
            'project_id': fp.get_perms2().get_owner().replace('-', ''),
            'tenant_id': fp.get_perms2().get_owner().replace('-', ''),
            'shared': self._is_shared(fp),
            'audited': fp.get_id_perms().get_enable(),
        }

        sorted_fr_refs = sorted(fp.get_firewall_rule_refs() or [],
                                key=lambda ref: float(ref['attr'].sequence))
        firewall_policy['firewall_rules'] = [
            ref['uuid'] for ref in sorted_fr_refs]

        return filter_fields(firewall_policy, fields)

    def firewall_policy_create(self, context, firewall_policy):
        """Create Firewall Policy

        Maps a Neutron Firewall Policy resource to a Contrail Firewall Policy.
        The FPs are a child of the project and are owned by that project.

        :param context: Neutron api request context
        :param firewall_policy: dictionary describing the Firewall Policy
        :returns: Firewall Policy dictionary populated
        """
        fp = self._firewall_policy_neutron_to_vnc(firewall_policy)

        try:
            id = self._resource_create('firewall_policy', fp)
        except BadRequest as e:
            self._raise_contrail_exception(
                'BadRequest', resource='firewall_policy', msg=str(e))

        return self._firewall_policy_vnc_to_neutron(
            self._vnc_lib.firewall_policy_read(id=id))

    def firewall_policy_read(self, context, id, fields=None):
        """Retrieve Firewall Policy

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Policy to fetch
        :param fields: a list of strings that are valid keys in a Firewall
            Policy dictionary. Only these fields will be returned.
        :returns: Firewall Policy dictionary
        """
        try:
            fp = self._vnc_lib.firewall_policy_read(id=id)
        except NoIdError:
            self._raise_contrail_exception('FirewallPolicyNotFound',
                                           firewall_policy_id=id)

        return self._firewall_policy_vnc_to_neutron(fp, fields)

    def firewall_policy_list(self, context, filters=None, fields=None):
        """Retrieve a list of Firewall Policy

        :param context: Neutron api request context
        :param filters: a dictionary with keys that are valid keys for a
            Firewall Policy
        :param fields: a list of strings that are valid keys in a Firewall
            Policy dictionary. Only these fields will be returned.
        :returns: Firewall Policy dictionary
        """
        results = []
        if not filters:
            filters = {}
        if 'name' in filters:
            filters['display_name'] = filters.pop('name')
        shared = filters.pop('shared', [False])[0]
        parent_ids = self._validate_project_ids(context, filters)
        filters.pop('tenant_id', None)
        filters.pop('project_id', None)
        fps = self._vnc_lib.firewall_policys_list(
            detail=True,
            shared=shared,
            parent_id=parent_ids,
            obj_uuids=filters.pop('id', None),
            back_ref_id=filters.pop('firewall_rules', None),
            filters=filters)
        for fp in fps:
            if (shared and fp.get_perms2().owner.replace('-', '') ==
                    context['tenant_id']):
                continue
            results.append(self._firewall_policy_vnc_to_neutron(fp, fields))

        return results

    def firewall_policy_update(self, context, id, firewall_policy):
        """Update value of Firewall Policy

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Policy to update
        :param firewall_policy: dictionary with keys indicating fields to
            update
        :returns: Firewall Policy dictionary updated
        """
        # if the update request does not set the audited flag, update it to
        # False to warm a new policy audit is needed
        firewall_policy.setdefault('audited', False)

        fp = self._firewall_policy_neutron_to_vnc(firewall_policy, id)

        self._resource_update('firewall_policy', fp)

        return self._firewall_policy_vnc_to_neutron(
            self._vnc_lib.firewall_policy_read(id=id))

    def firewall_policy_delete(self, context, id):
        """Delete Firewall Policy

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Policy to delete
        """
        try:
            self._vnc_lib.firewall_policy_delete(id=id)
        except NoIdError:
            self._raise_contrail_exception('FirewallPolicyNotFound',
                                           firewall_rule_id=id)
        except RefsExistError:
            self._raise_contrail_exception('FirewallPolicyInUse',
                                           firewall_policy_id=id)

    def firewall_policy_insert_rule(self, context, id, rule_info):
        """Insert firewall rule into a policy

        A `firewall_rule_id` is inserted relative to the position of the
        `firewall_rule_id` set in `insert_before` or `insert_after`. If
        `insert_before` is set, `insert_after` is ignored. If both
        `insert_before` and `insert_after` are not set, the new
        `firewall_rule_id` is inserted as the first rule of the policy.

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Policy to update
        :param rule_info: dictionary with keys indicating how to insert
            Firewall Rule. Valid dictionay keys are `firewall_rule_id`,
            `insert_before` and `insert_after`
        :returns: Firewall Policy dictionary updated
        """
        firewall_rule_id = rule_info['firewall_rule_id']
        insert_before = True
        ref_firewall_rule_id = None
        if 'insert_before' in rule_info:
            ref_firewall_rule_id = rule_info['insert_before']
        if not ref_firewall_rule_id and 'insert_after' in rule_info:
            # If insert_before is set, we will ignore insert_after.
            ref_firewall_rule_id = rule_info['insert_after']
            insert_before = False

        firewall_rule_list = self.firewall_policy_read(
            context, id, ['firewall_rules'])['firewall_rules']
        if ref_firewall_rule_id:
            if ref_firewall_rule_id not in firewall_rule_list:
                self._raise_contrail_exception(
                    'FirewallRuleNotFound',
                    firewall_rule_id=ref_firewall_rule_id)
            position = firewall_rule_list.index(ref_firewall_rule_id)
            if not insert_before:
                position += 1
        else:
            position = 0
        new_firewall_rule_list = copy.copy(firewall_rule_list)
        try:
            new_firewall_rule_list.remove(firewall_rule_id)
        except ValueError:
            pass
        new_firewall_rule_list.insert(position, firewall_rule_id)

        if new_firewall_rule_list != firewall_rule_list:
            return self.firewall_policy_update(
                context, id, {'firewall_rules': new_firewall_rule_list})
        else:
            return self.firewall_policy_read(contect, id)

    def firewall_policy_remove_rule(self, context, id, rule_info):
        """Remove firewall rule from a policy

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Policy to update
        :param rule_info: dictionary with key `firewall_rule_id` indicating
            Firewall Rule to remove
        :returns: Firewall Policy dictionary updated
        """
        firewall_rule_id = rule_info['firewall_rule_id']
        firewall_rule_list = self.firewall_policy_read(
            context, id, ['firewall_rules'])['firewall_rules']
        try:
            firewall_rule_list.remove(firewall_rule_id)
        except ValueError:
            self._raise_contrail_exception(
                'FirewallRuleNotAssociatedWithPolicy',
                firewall_rule_id=firewall_rule_id,
                firewall_policy_id=id)
        return self.firewall_policy_update(
            context, id, {'firewall_rules': firewall_rule_list})

    def _get_port_type(self, port_range_str):
        """Convert port range string to vnc_api.gen.resource_xsd.PortType"""
        if port_range_str:
            try:
                port_min, _, port_max = port_range_str.partition(':')
                if not port_max:
                    port_max = port_min
                return PortType(int(port_min), int(port_max))
            except ValueError:
                self._raise_contrail_exception('FirewallRuleInvalidPortValue',
                                               port=port_range_str)

    def _get_subnet_type(self, subnet_str):
        """Convert subnet string to vnc_api.gen.resource_xsd.SubnetType"""
        if subnet_str:
            try:
                ip_network = netaddr.IPNetwork(subnet_str)
            except netaddr.core.AddrFormatError:
                msg = ("'%s' is neither a valid IP address, nor is it a valid "
                       "IP subnet" % subnet_str)
                self._raise_contrail_exception(
                    'BadRequest', resource='firewall_rule', msg=msg)
            return (ip_network.version,
                    SubnetType(str(ip_network.network), ip_network.prefixlen))

    @staticmethod
    def _get_tag_list(firewall_group_id):
        """Returns application Tag name dedicated to a Firewall Group"""
        if firewall_group_id:
            return ['%s=%s' % (_NEUTRON_FWAAS_TAG_TYPE, firewall_group_id)]

    def _get_action(self, action):
        """Convert action string to vnc_api.gen.resource_xsd.ActionListType"""
        if action:
            if action == 'allow':
                action = 'pass'
            available_actions = ActionListType.\
                attr_field_type_vals['simple_action']['restrictions']
            if action not in available_actions:
                self._raise_contrail_exception(
                    'FirewallRuleInvalidAction',
                    action=action,
                    values=', '.join(available_actions))
            return ActionListType(simple_action=action)

    def _firewall_rule_neutron_to_vnc(self, firewall_rule, id=None):
        """Neutron Firewall Rule resource to Contrail Firewall Rule

        =============================  ======================
        Neutron Firewall Rule          Contrail Firewall Rule
        =============================  ======================
        id                             `uuid`
        tenant_id                      `project` parent reference and owner
        name                           `display_name`
        enabled                        `id_perms.enable`
        description                    `id_perms.description`
        share                          `perms2`
        firewall_policy_id             `firewall_policy` back-reference
        ip_version                     IP version determined by
                                       `source_ip_address` and
                                       `destination_ip_address`
        source_ip_address              `endpoint_1.subnet`
        source_firewall_group_id       `endpoint_1.tags`
        destination_ip_address         `endpoint_2.subnet`
        destination_firewall_group_id  `endpoint_2.tags`
        protocol                       `service.protocol`
        source_port                    `service.src_ports`
        destination_port               `service.dst_ports`
        position                       Not Implemented as rule can be
                                       referenced by multiple policies
        action                         `action_list.simple_action`
        =============================  ======================

        :param firewall_rule: Firewall Rule dictionary to convert
        :param id: permits to read FR if method called during an update
            request. If `None`, instantiate new FR
        :returns: vnc_api.gen.resource_client.FirewallRule
        """
        # cannot use default firewall rule names
        if (firewall_rule.get('name') in
                [_NEUTRON_FIREWALL_DEFAULT_IPV4_RULE_NAME,
                 _NEUTRON_FIREWALL_DEFAULT_IPV6_RULE_NAME]):
            vnc_openstack.ensure_default_firewall_group(
                self._vnc_lib, firewall_rule['tenant_id'])
            msg = ("Default firewall rule already exists. 'default' is the "
                   "reserved name for firewall rule.")
            self._raise_contrail_exception(
                'BadRequest', resource='firewall_rule', msg=msg)

        if not id:  # creation
            project = self._get_project_obj(firewall_rule)
            project_uuid = project.uuid
            fr = FirewallRule(
                name=firewall_rule.get('name') or str(uuid.uuid4()),
                parent_obj=project,
                perms2=PermType2(owner=project.uuid),
                service=FirewallServiceType(protocol='any'),
                direction='>',
            )
        else:  # update
            try:
                fr = self._vnc_lib.firewall_rule_read(id=id)
            except NoIdError:
                self._raise_contrail_exception('FirewallRuleNotFound',
                                               firewall_rule_id=id)
            project_uuid = fr.parent_uuid

        if 'name' in firewall_rule:
            fr.set_display_name(firewall_rule['name'])

        if set(['enabled', 'description']) & set(firewall_rule.keys()):
            id_perms = fr.get_id_perms() or IdPermsType(enable=True)
            if 'enabled' in firewall_rule:
                id_perms.set_enable(firewall_rule['enabled'])
            if 'description' in firewall_rule:
                id_perms.set_description(firewall_rule['description'])
            fr.set_id_perms(id_perms)

        if firewall_rule.get('shared'):
            perms2 = fr.get_perms2() or PermType2(owner=project_uuid)
            perms2.set_global_access(PERMS_RWX)
            fr.set_perms2(perms2)

        ip_src_version = None
        if (set(['source_ip_address', 'source_firewall_group_id']) &
                set(firewall_rule.keys())):
            ep1 = FirewallRuleEndpointType()
            src_prefix = firewall_rule.get('source_ip_address')
            src_fg = firewall_rule.get('source_firewall_group_id')
            if src_prefix and src_fg:
                msg = ("Firewall rule cannot have a source IP address and a"
                       "source firewall group ID in a mean time.")
                self._raise_contrail_exception(
                    'BadRequest', resource='firewall_rule', msg=msg)
            elif src_prefix:
                ip_src_version, subnet_type = self._get_subnet_type(src_prefix)
                ep1.set_subnet(subnet_type)
            elif src_fg:
                ep1.set_tags(self._get_tag_list(src_fg))
            else:
                ep1.set_any(True)
            fr.set_endpoint_1(ep1)

        ip_dst_version = None
        if (set(['destination_ip_address', 'destination_firewall_group_id']) &
                set(firewall_rule.keys())):
            ep2 = FirewallRuleEndpointType()
            dst_prefix = firewall_rule.get('destination_ip_address')
            dst_fg = firewall_rule.get('destination_firewall_group_id')
            if dst_prefix and dst_fg:
                msg = ("Firewall rule cannot have a destination IP address "
                       "and a destination firewall group ID in a mean time.")
                self._raise_contrail_exception(
                    'BadRequest', resource='firewall_rule', msg=msg)
            elif dst_prefix:
                ip_dst_version, subnet_type = self._get_subnet_type(dst_prefix)
                ep2.set_subnet(subnet_type)
            elif dst_fg:
                ep2.set_tags(self._get_tag_list(dst_fg))
            else:
                ep2.set_any(True)
            fr.set_endpoint_2(ep2)

        if (ip_src_version and ip_dst_version and
                ip_src_version != ip_dst_version):
            self._raise_contrail_exception('FirewallIpAddressConflict')

        ip_version = firewall_rule.get('ip_version')
        if (ip_version and
                ((ip_src_version and ip_version != ip_src_version) or
                 (ip_dst_version and ip_version != ip_dst_version))):
            self._raise_contrail_exception('FirewallIpAddressConflict')

        ip_version = ip_version or ip_src_version or ip_dst_version
        if (set(['protocol', 'source_port', 'destination_port']) &
                set(firewall_rule.keys())):
            service = fr.get_service() or FirewallServiceType(protocol='any')
            if 'protocol' in firewall_rule:
                if firewall_rule['protocol'] == 'icmp' and ip_version == 6:
                    service.set_protocol('ipv6-icmp')
                elif (firewall_rule['protocol'] == 'ipv6-icmp' and
                        ip_version == 4):
                    service.set_protocol('icmp')
                else:
                    service.set_protocol(firewall_rule['protocol'] or 'any')
            if 'source_port' in firewall_rule:
                service.set_src_ports(
                    self._get_port_type(firewall_rule['source_port']))
            if 'destination_port' in firewall_rule:
                service.set_dst_ports(
                    self._get_port_type(firewall_rule['destination_port']))
            fr.set_service(service)

        if 'action' in firewall_rule:
            fr.set_action_list(self._get_action(firewall_rule['action']))

        return fr

    @staticmethod
    def _get_port_range_str(port_type):
        """Convert vnc_api.gen.resource_xsd.PortType to port string"""
        if port_type:
            port_min = port_type.get_start_port()
            port_max = port_type.get_end_port()
            if port_min == port_max:
                return str(port_min)
            return '%d:%d' % (port_min, port_max)

    @staticmethod
    def _get_ip_prefix_str(endpoint_type):
        """Convert vnc_api.gen.resource_xsd.FirewallRuleEndpointType to subnet
        string
        """
        if endpoint_type.get_any():
             return None
        subnet_type = endpoint_type.get_subnet()
        if subnet_type:
            return '%s/%d' % (subnet_type.get_ip_prefix(),
                              subnet_type.get_ip_prefix_len())
        return None

    @staticmethod
    def _get_firewall_group_id(tags):
        """Returns first found Firewall Group ID from Tag list"""
        for tag in tags or []:
            tag_type, _, tag_value = tag.partition('=')
            if tag_type == _NEUTRON_FWAAS_TAG_TYPE and is_uuid_like(tag_value):
                return tag_value

    @staticmethod
    def _get_action_str(action):
        """Convert vnc_api.gen.resource_xsd.ActionListType to action string"""
        if action:
            action_str = action.get_simple_action()
            if action_str == 'pass':
                return 'allow'
            return action_str

    def _firewall_rule_vnc_to_neutron(self, fr, fields=None):
        """Convert Contrail Firewall Rule to Neutron Firewall Rule

        :param fr: vnc_api.gen.resource_client.FirewallRule instance to
            convert
        :param fields: a list of strings that are valid keys in a Firewall Rule
            dictionary. Only these fields will be returned.
        :retruns: Firewall Rule dictionary
        """
        firewall_rule = {
            'id': fr.uuid,
            'name': fr.display_name,
            'description': fr.get_id_perms().get_description(),
            'enabled': fr.get_id_perms().get_enable(),
            'project_id': fr.get_perms2().get_owner().replace('-', ''),
            'tenant_id': fr.get_perms2().get_owner().replace('-', ''),
            'shared': self._is_shared(fr),
            'firewall_policy_id': [fp_ref['uuid'] for fp_ref in
                                   fr.get_firewall_policy_back_refs() or []],
        }

        service = fr.get_service()
        if service:
            protocol = service.get_protocol()
            firewall_rule.update({
                'protocol': 'icmp' if protocol == 'ipv6-icmp' else protocol,
                'source_port':
                    self._get_port_range_str(service.get_src_ports()),
                'destination_port':
                    self._get_port_range_str(service.get_dst_ports()),
            })

        firewall_rule['ip_version'] = 4
        for target, ep_name in [('source', 'endpoint_1'),
                                ('destination', 'endpoint_2')]:
            ep = getattr(fr, 'get_%s' % ep_name)()
            if not ep:
                continue
            ip_address = self._get_ip_prefix_str(ep)
            firewall_rule.update({
                '%s_ip_address' % target: ip_address,
                '%s_firewall_group_id' % target:
                    self._get_firewall_group_id(ep.get_tags()),
            })
            if ip_address:
                firewall_rule['ip_version'] = netaddr.IPNetwork(
                    ip_address).version

        action = fr.get_action_list()
        if action:
            firewall_rule['action'] = self._get_action_str(action)

        return filter_fields(firewall_rule, fields)

    def firewall_rule_create(self, context, firewall_rule):
        """Create Firewall Rule

        Maps a Neutron Firewall Rule resource to a Contrail Firewall Rule. The
        FRs are a child of the project and are owned by that project.

        :param context: Neutron api request context
        :param firewall_rule: dictionary describing the Firewall Rule
        :returns: Firewall Rule dictionary populated
        """
        fr = self._firewall_rule_neutron_to_vnc(firewall_rule)

        try:
            id = self._resource_create('firewall_rule', fr)
        except BadRequest as e:
            self._raise_contrail_exception(
                'BadRequest', resource='firewall_rule', msg=str(e))

        return self._firewall_rule_vnc_to_neutron(
            self._vnc_lib.firewall_rule_read(id=id))

    def firewall_rule_read(self, context, id, fields):
        """Retrieve Firewall Rule

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Rule to fetch
        :param fields: a list of strings that are valid keys in a Firewall Rule
            dictionary. Only these fields will be returned.
        :returns: Firewall Rule dictionary
        """
        try:
            fr = self._vnc_lib.firewall_rule_read(id=id)
        except NoIdError:
            self._raise_contrail_exception('FirewallRuleNotFound',
                                           firewall_rule_id=id)

        return self._firewall_rule_vnc_to_neutron(fr, fields)

    def firewall_rule_list(self, context, filters, fields):
        """Retrieve a list of Firewall Rule

        :param context: Neutron api request context
        :param filters: a dictionary with keys that are valid keys for a
            Firewall Rule
        :param fields: a list of strings that are valid keys in a Firewall Rule
            dictionary. Only these fields will be returned.
        :returns: Firewall Rule dictionary
        """
        results = []
        if not filters:
            filters = {}
        if 'name' in filters:
            filters['display_name'] = filters.pop('name')
        shared = filters.pop('shared', [False])[0]
        parent_ids = self._validate_project_ids(context, filters)
        filters.pop('tenant_id', None)
        filters.pop('project_id', None)
        frs = self._vnc_lib.firewall_rules_list(
            detail=True,
            shared=shared,
            parent_id=parent_ids,
            obj_uuids=filters.pop('id', None),
            filters=filters)
        for fr in frs:
            if (shared and fr.get_perms2().owner.replace('-', '') ==
                    context['tenant_id']):
                continue
            firewall_rule = self._firewall_rule_vnc_to_neutron(fr, fields)
            if ('firewall_policy_id' in filters and
                    not set(firewall_rule['firewall_policy_id']) &
                    set(filters['firewall_policy_id'])):
                continue
            results.append(firewall_rule)

        return results

    def firewall_rule_update(self, context, id, firewall_rule):
        """Update value of Firewall Rule

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Rule to update
        :param firewall_rule: dictionary with keys indicating fields to update
        :returns: Firewall Rule dictionary updated
        """
        fr = self._firewall_rule_neutron_to_vnc(firewall_rule, id)

        self._resource_update('firewall_rule', fr)
        firewall_rule = self._firewall_rule_vnc_to_neutron(
            self._vnc_lib.firewall_rule_read(id=id))

        for firewall_policy_id in firewall_rule.get('firewall_policy_id', []):
            self.firewall_policy_update(
                context, firewall_policy_id, {'audited': False})
        return firewall_rule

    def firewall_rule_delete(self, context, id):
        """Delete Firewall Rule

        :param context: Neutron api request context
        :param id: UUID representing the Firewall Rule to delete
        """
        try:
            self._vnc_lib.firewall_rule_delete(id=id)
        except NoIdError:
            self._raise_contrail_exception('FirewallRuleNotFound',
                                           firewall_rule_id=id)
        except RefsExistError:
            self._raise_contrail_exception(
                'FirewallRuleInUse', firewall_rule_id=id)

    def _trunk_neutron_to_vnc(self, trunk_q, oper, id=None,
                              port_operation=None):
        """Convert Neutron Trunk Port to Contrail Virtual Port Group

        :param trunk_q: Trunk Port dictionary to convert
        :param oper: operation i.e CRUD to perform on trunk port
        :returns: vnc_api.gen.resource_client.VirtualPortGroup
        """

        sub_vmi_vlan_tags = []
        def _set_sub_ports(trunk, trunk_port, sub_port_dict):

            sp_id = sub_port_dict['port_id']
            if sp_id == trunk_port.uuid:
                self._raise_contrail_exception('PortInUseAsTrunkParent',
                                               port_id=sp_id,
                                               trunk_id=id)
            try:
                sub_port = self._vnc_lib.virtual_machine_interface_read(
                    id=sp_id)
            except NoIdError:
                self._raise_contrail_exception('SubPortNotFound',
                                               trunk_id=id,
                                               port_id=sp_id)

            if ('segmentation_type' in sub_port_dict and
                sub_port_dict['segmentation_type'] != 'vlan'):
                msg = "Segmentation type %s is not supported" \
                      %(sub_port_dict['segmentation_type'])
                self._raise_contrail_exception('BadRequest',
                    resource='trunk', msg=msg)

            if 'segmentation_id' in sub_port_dict:
                if sub_port_dict['segmentation_id'] in sub_vmi_vlan_tags:
                    self._raise_contrail_exception(
                        'DuplicateSubPort',
                        trunk_id=id,
                        segmentation_id=sub_port_dict['segmentation_id'],
                        segmentation_type='vlan')

                sub_vmi_vlan_tags.append(sub_port_dict['segmentation_id'])
                vmi_prop = VirtualMachineInterfacePropertiesType(
                    sub_interface_vlan_tag=sub_port_dict['segmentation_id'])
                sub_port.set_virtual_machine_interface_properties(vmi_prop)
                self._vnc_lib.virtual_machine_interface_update(sub_port)

            trunk.add_virtual_machine_interface(sub_port)
            trunk_port.add_virtual_machine_interface(sub_port)
            self._vnc_lib.virtual_machine_interface_update(trunk_port)

        if oper == CREATE:
            if 'name' in trunk_q:
                name = trunk_q.get('name')
            project = self._get_project_obj(trunk_q)
            port_id = trunk_q.get('port_id')
            trunk = VirtualPortGroup(name=name, parent_obj=project,
                                  virtual_port_group_trunk_port_id=port_id)
            try:
                trunk_port = self._vnc_lib.virtual_machine_interface_read(
                id=port_id)
            except:
                self._raise_contrail_exception('PortNotFound',
                                               port_id=port_id)

            if 'name' in trunk_q:
                trunk.display_name = trunk_q.get('name')

            trunk.add_virtual_machine_interface(trunk_port)
            if trunk_q.get('sub_ports'):
                for sp in trunk_q['sub_ports']:
                    _set_sub_ports(trunk, trunk_port, sp)

            trunk.set_id_perms(IdPermsType(enable=True))
            trunk.set_perms2(PermType2(owner=project.uuid))

        elif oper == UPDATE:
            try:
                trunk = self._vnc_lib.virtual_port_group_read(id=id)
            except NoIdError:
                self._raise_contrail_exception('TrunkNotFound',
                                               trunk_id=id)
            if 'name' in trunk_q:
                trunk.display_name = trunk_q['name']

            id_perms = trunk.get_id_perms()
            if 'admin_state_up' in trunk_q:
                id_perms.enable = trunk_q['admin_state_up']
                trunk.set_id_perms(id_perms)

            if 'sub_ports' in trunk_q:
                try:
                    trunk_port = self._vnc_lib.virtual_machine_interface_read(
                        id=trunk.virtual_port_group_trunk_port_id)
                except:
                    self._raise_contrail_exception(
                        'PortNotFound',
                        port_id=trunk.virtual_port_group_trunk_port_id)

                if port_operation == 'ADD_SUBPORTS':
                    sub_vmi_refs = trunk.get_virtual_machine_interface_refs()
                    sub_vmi_uuids = [ref['uuid'] for ref in sub_vmi_refs]
                    if sub_vmi_uuids:
                        sub_vmis = self._virtual_machine_interface_list(
                            obj_uuids=sub_vmi_uuids,
                            fields=['virtual_machine_interface_properties'])

                        for vmi in sub_vmis:
                            prop = vmi.get_virtual_machine_interface_properties()
                            if prop:
                                sub_vmi_vlan_tags.append(
                                    prop.get_sub_interface_vlan_tag())

                    for sp in trunk_q['sub_ports']:
                        _set_sub_ports(trunk, trunk_port, sp)

                elif port_operation == 'REMOVE_SUBPORTS':
                    for sp in trunk_q['sub_ports']:
                        try:
                            sub_port = self._vnc_lib.virtual_machine_interface_read(
                                id=sp['port_id'])
                        except NoIdError:
                            self._raise_contrail_exception('SubPortNotFound',
                                                           trunk_id=id,
                                                           port_id=sp_id)
                        trunk.del_virtual_machine_interface(sub_port)
                        trunk_port.del_virtual_machine_interface(sub_port)
                        self._vnc_lib.virtual_machine_interface_update(
                            trunk_port)

        elif oper == DELETE:
            try:
                trunk = self._vnc_lib.virtual_port_group_read(id=id)
            except NoIdError:
                self._raise_contrail_exception('TrunkNotFound',
                                               trunk_id=id)

            try:
                trunk_port = self._vnc_lib.virtual_machine_interface_read(
                    id=trunk.virtual_port_group_trunk_port_id)
            except:
                self._raise_contrail_exception(
                    'PortNotFound',
                    port_id=trunk.virtual_port_group_trunk_port_id)

            if trunk_port.get_virtual_machine_interface_refs():
                self._raise_contrail_exception('TrunkInUse', trunk_id=id)

            trunk.del_virtual_machine_interface(trunk_port)
            self._vnc_lib.virtual_port_group_update(trunk)

        return trunk

    @catch_convert_exception
    def _trunk_vnc_to_neutron(self, trunk_obj, fields=None):
        trunk_q_dict = {}
        extra_dict = {}

        port_id = trunk_obj.virtual_port_group_trunk_port_id
        port_obj = self._vnc_lib.virtual_machine_interface_read(id=port_id)
        sub_ports = []
        if trunk_obj.get_virtual_machine_interface_refs():
            for sub_port in trunk_obj.get_virtual_machine_interface_refs():
                # To not include parent/trunk port from a sub port list
                if sub_port['uuid'] == port_id:
                    continue
                sub_port_obj = self._vnc_lib.virtual_machine_interface_read(id=sub_port['uuid'])

                vmi_prop = sub_port_obj.get_virtual_machine_interface_properties()
                vlan_tag = vmi_prop.get_sub_interface_vlan_tag() if vmi_prop else None
                sub_ports.append({'port_id': sub_port['uuid'],
                                  'segmentation_id' : vlan_tag,
                                  'segmentation_type': 'vlan'})

        trunk_q_dict = {
            'id': trunk_obj.uuid,
            'description': trunk_obj.get_id_perms().get_description(),
            'admin_state_up': trunk_obj.get_id_perms().get_enable(),
            'project_id': trunk_obj.get_perms2().get_owner().replace('-', ''),
            'tenant_id': trunk_obj.get_perms2().get_owner().replace('-', ''),
            'created_at': trunk_obj.get_id_perms().get_created(),
            'updated_at': trunk_obj.get_id_perms().get_last_modified(),
            'port_id': port_id,
            'sub_ports': sub_ports,
            'status': self._port_get_interface_status(port_obj),
            'name': trunk_obj.display_name
            }

        return filter_fields(trunk_q_dict, fields)

    @wait_for_api_server_connection
    def trunk_create(self, context, trunk_q):

        trunk_obj = self._trunk_neutron_to_vnc(trunk_q, CREATE)
        try:
            id = self._resource_create('virtual_port_group', trunk_obj)
        except BadRequest as e:
            self._raise_contrail_exception(
                'BadRequest', resource='virtual_port_group', msg=str(e))
        except RefsExistError:
            self._raise_contrail_exception('TrunkPortInUse',
                                           port_id=trunk_q.get('port_id'))

        ret_trunk_q = self._trunk_vnc_to_neutron(trunk_obj)
        return ret_trunk_q

    @wait_for_api_server_connection
    def trunk_read(self, id, fields=None, context=None):

        try:
            trunk = self._vnc_lib.virtual_port_group_read(id=id)
        except NoIdError:
            self._raise_contrail_exception('TrunkNotFound',
                                           trunk_id=id)

        return self._trunk_vnc_to_neutron(trunk, fields=fields)

    @wait_for_api_server_connection
    def trunk_update(self, context, id, trunk_q):
        """Update value of Trunk

        :param context: Neutron api request context
        :param id: UUID representing the Trunk to update
        :param trunk_q: dictionary with keys indicating fields to update
        :returns: Trunk dictionary updated
        """
        trunk_obj = self._trunk_neutron_to_vnc(trunk_q, UPDATE, id)
        self._resource_update('virtual_port_group', trunk_obj)
        ret_trunk_q = self._trunk_vnc_to_neutron(
            self._vnc_lib.virtual_port_group_read(id=id))
        return ret_trunk_q

    @wait_for_api_server_connection
    def trunk_delete(self, context, id):
        """Delete Trunk

        :param context: Neutron api request context
        :param id: UUID representing the Trunk object to delete
        """
        try:
            self._trunk_neutron_to_vnc({}, DELETE, id=id)
            self._vnc_lib.virtual_port_group_delete(id=id)
        except NoIdError:
            self._raise_contrail_exception('TrunkNotFound', trunk_id=id)
        except RefsExistError:
            self._raise_contrail_exception('TrunkInUse', trunk_id=id)

    @wait_for_api_server_connection
    def trunk_list(self, context, filters, fields):
        """Retrieve a list of Trunk

        :param context: Neutron api request context
        :param filters: a dictionary with keys that are valid keys for a
            Trunk
        :param fields: a list of strings that are valid keys in a Trunk
            dictionary. Only these fields will be returned.
        :returns: Trunk dictionary
        """
        results = []
        if 'name' in filters:
            filters['display_name'] = filters.pop('name')
        shared = filters.pop('shared', [False])[0]
        parent_ids = self._validate_project_ids(context, filters)
        filters.pop('tenant_id', None)
        filters.pop('project_id', None)
        trunks = self._vnc_lib.virtual_port_groups_list(
            detail=True,
            shared=shared,
            parent_id=parent_ids,
            obj_uuids=filters.pop('id', None),
            filters=filters)
        for trunk in trunks:
            if (shared and trunk.get_perms2().owner.replace('-', '') ==
                    context['tenant_id']):
                continue
            if trunk.parent_type != 'project':
                continue
            results.append(self._trunk_vnc_to_neutron(trunk, fields))

        return results


    @wait_for_api_server_connection
    def trunk_add_subports(self, context, id, sub_ports):
        """Add sub port to a trunk

        :param context: Neutron api request context
        :param id: UUID representing the Trunk object
        :param sub_ports: sub ports info that needs to be added
        to trunk object.
        """
        port_operation = context.get('operation')
        trunk_obj = self._trunk_neutron_to_vnc(sub_ports, UPDATE,
                                               id, port_operation)
        self._resource_update('virtual_port_group', trunk_obj)
        ret_trunk_q = self._trunk_vnc_to_neutron(
            self._vnc_lib.virtual_port_group_read(id=id))
        return ret_trunk_q

    @wait_for_api_server_connection
    def trunk_remove_subports(self, context, id, sub_ports):
        """Remove sub port from a trunk

        :param context: Neutron api request context
        :param id: UUID representing the Trunk object
        :param sub_ports: sub ports info that needs to be removed from a
         trunk object.
        """
        port_operation = context.get('operation')
        trunk_obj = self._trunk_neutron_to_vnc(sub_ports, UPDATE,
                                               id, port_operation)
        self._resource_update('virtual_port_group', trunk_obj)
        ret_trunk_q = self._trunk_vnc_to_neutron(
            self._vnc_lib.virtual_port_group_read(id=id))
        return ret_trunk_q
