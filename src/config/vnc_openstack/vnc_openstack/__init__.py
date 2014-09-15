#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import json
import uuid
import gevent
import requests
import cgitb
import copy
import bottle
import logging
import logging.handlers
import Queue
import ConfigParser
import keystoneclient.v2_0.client as keystone

import cfgm_common
try:
    from cfgm_common import vnc_plugin_base
except ImportError:
    from common import vnc_plugin_base
from vnc_api import vnc_api
from vnc_api.gen.resource_xsd import *

from vnc_api.gen.resource_xsd import *
from vnc_api.gen.resource_common import *

import neutron_plugin_interface as npi

Q_CREATE = 'create'
Q_DELETE = 'delete'
Q_MAX_ITEMS = 1000


def fill_keystone_opts(obj, conf_sections):
    obj._auth_user = conf_sections.get('KEYSTONE', 'admin_user')
    obj._auth_passwd = conf_sections.get('KEYSTONE', 'admin_password')
    obj._admin_token = conf_sections.get('KEYSTONE', 'admin_token')
    obj._admin_tenant = conf_sections.get('KEYSTONE', 'admin_tenant_name')
    try:
        obj._keystone_sync_on_demand = conf_sections.getboolean('KEYSTONE',
                                               'keystone_sync_on_demand')
    except ConfigParser.NoOptionError:
        obj._keystone_sync_on_demand = True

    try:
        obj._insecure = conf_sections.getboolean('KEYSTONE', 'insecure')
    except ConfigParser.NoOptionError:
        obj._insecure = True

    try:
        obj._auth_url = conf_sections.get('KEYSTONE', 'auth_url')
    except ConfigParser.NoOptionError:
        # deprecated knobs - for backward compat
        obj._auth_proto = conf_sections.get('KEYSTONE', 'auth_protocol')
        obj._auth_host = conf_sections.get('KEYSTONE', 'auth_host')
        obj._auth_port = conf_sections.get('KEYSTONE', 'auth_port')
        obj._auth_url = "%s://%s:%s/v2.0" % (obj._auth_proto, obj._auth_host,
                                             obj._auth_port)


class OpenstackDriver(vnc_plugin_base.Resync):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        if api_server_ip == '0.0.0.0':
            self._vnc_api_ip = '127.0.0.1'
        else:
            self._vnc_api_ip = api_server_ip

        self._vnc_api_port = api_server_port

        self._config_sections = conf_sections
        fill_keystone_opts(self, conf_sections)

        if 'v3' in self._auth_url.split('/')[-1]:
            self._get_keystone_conn = self._ksv3_get_conn
            self._ks_domains_list = self._ksv3_domains_list
            self._ks_domain_get = self._ksv3_domain_get
            self._ks_projects_list = self._ksv3_projects_list
            self._ks_project_get = self._ksv3_project_get
            self.sync_project_to_vnc = self._ksv3_sync_project_to_vnc
            self._add_project_to_vnc = self._ksv3_add_project_to_vnc
            self._del_project_from_vnc = self._ksv3_del_project_from_vnc
            self._vnc_default_domain_id = None
        else:
            self._get_keystone_conn = self._ksv2_get_conn
            self._ks_domains_list = None
            self._ks_domain_get = None
            self._ks_projects_list = self._ksv2_projects_list
            self._ks_project_get = self._ksv2_project_get
            self.sync_project_to_vnc = self._ksv2_sync_project_to_vnc
            self._add_project_to_vnc = self._ksv2_add_project_to_vnc
            self._del_project_from_vnc = self._ksv2_del_project_from_vnc
        
        self._resync_interval_secs = 2
        self._resync_number_workers = 10 #TODO(sahid) needs to be configured by conf.
        self._ks = None
        self._vnc_lib = None

        # resync failures, don't retry forever
        self._failed_domain_dels = set()
        self._failed_project_dels = set()

        # active domains/projects in contrail/vnc api server
        self._vnc_domain_ids = set()
        self._vnc_project_ids = set()

        # logging
        self._log_file_name = '/var/log/contrail/vnc_openstack.err'
        self._tmp_file_name = '/var/log/contrail/vnc_openstack.tmp'
        self._vnc_os_logger = logging.getLogger(__name__)
        self._vnc_os_logger.setLevel(logging.ERROR)
        # Add the log message handler to the logger
        try:
            handler = logging.handlers.RotatingFileHandler(self._log_file_name,
                                                           maxBytes=1024,
                                                           backupCount=5)
        except IOError:
            self._log_file_name = './vnc_openstack.err'
            self._tmp_file_name = './vnc_openstack.tmp'
            handler = logging.handlers.RotatingFileHandler(self._log_file_name,
                                                           maxBytes=1024,
                                                           backupCount=5)

        self._vnc_os_logger.addHandler(handler)
        self.q = Queue.Queue(maxsize=Q_MAX_ITEMS)
    #end __init__

    def __call__(self):
        pass
    #end __call__

    def _get_vnc_conn(self):
        if self._vnc_lib:
            return

        self._vnc_lib = vnc_api.VncApi(
            api_server_host=self._vnc_api_ip,
            api_server_port=self._vnc_api_port,
            username=self._auth_user,
            password=self._auth_passwd,
            tenant_name=self._admin_tenant)
    # end _get_vnc_conn

    def _ksv2_get_conn(self):
        if not self._ks:
            if self._admin_token:
                self._ks = keystone.Client(token=self._admin_token,
                                           endpoint=self._auth_url,
                                           insecure=self._insecure)
            else:
                self._ks = keystone.Client(username=self._auth_user,
                                           password=self._auth_passwd,
                                           tenant_name=self._admin_tenant,
                                           auth_url=self._auth_url,
                                           insecure=self._insecure)
    # end _ksv2_get_conn

    def _ksv2_projects_list(self):
        return [{'id': tenant.id} for tenant in self._ks.tenants.list()]
    # end _ksv2_projects_list

    def _ksv2_project_get(self, id):
        return {'name': self._ks.tenants.get(id).name}
    # end _ksv2_project_get

    def _ksv2_sync_project_to_vnc(self, id=None):
        self._get_keystone_conn()
        self._get_vnc_conn()
        ks_project = self._ks_project_get(id=id.replace('-', ''))
        display_name = ks_project['name']

        # if earlier project exists with same name but diff id,
        # create with uniqified fq_name
        fq_name = ['default-domain', display_name]
        try:
            old_id = self._vnc_lib.fq_name_to_id('project', fq_name)
            proj_name = '%s-%s' %(display_name, str(uuid.uuid4()))
        except vnc_api.NoIdError:
            proj_name = display_name

        proj_obj = vnc_api.Project(proj_name)
        proj_obj.uuid = id
        self._vnc_lib.project_create(proj_obj)
    # end _ksv2_sync_project_to_vnc

    def _ksv2_add_project_to_vnc(self, project_id):
        try:
            self._vnc_lib.project_read(id=project_id)
            # project exists, no-op for now,
            # sync any attr changes in future
        except vnc_api.NoIdError:
            self._ksv2_sync_project_to_vnc(project_id)
    # _ksv2_add_project_to_vnc

    def _ksv2_del_project_from_vnc(self, project_id):
        if project_id in self._failed_project_dels:
            return

        try:
            self._vnc_lib.project_delete(id=project_id)
        except vnc_api.NoIdError:
            pass
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())
            self._failed_project_dels.add(project_id)
    # _ksv2_del_project_from_vnc

    def _ksv3_get_conn(self):
        if self._ks:
            return

        self._ks = requests.Session()
        adapter = requests.adapters.HTTPAdapter()
        self._ks.mount("http://", adapter)
        self._ks.mount("https://", adapter)
    # end _ksv3_get_conn

    def _ksv3_domains_list(self):
        resp = self._ks.get('%s/domains' %(self._auth_url),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        domains_json = resp.text
        return json.loads(domains_json)['domains']
    # end _ksv3_domains_list

    def _ksv3_domain_id_to_uuid(self, domain_id):
        if domain_id == 'default':
            return self._vnc_default_domain_id

        return str(uuid.UUID(domain_id))
    # _ksv3_domain_id_to_uuid

    def _ksv3_domain_get(self, id=None):
        resp = self._ks.get('%s/domains/%s' %(self._auth_url, id),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        domain_json = resp.text
        return json.loads(domain_json)['domain']
    # end _ksv3_domain_get

    def _ksv3_projects_list(self):
        resp = self._ks.get('%s/projects' %(self._auth_url),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        projects_json = resp.text
        return json.loads(projects_json)['projects']
    # end _ksv3_projects_list

    def _ksv3_project_get(self, id=None):
        resp = self._ks.get('%s/projects/%s' %(self._auth_url, id),
                            headers={'X-AUTH-TOKEN':self._admin_token})
        if resp.status_code != 200:
            raise Exception(resp.text)

        project_json = resp.text
        return json.loads(project_json)['project']
    # end _ksv3_project_get

    def _ksv3_sync_project_to_vnc(self, id=None, name=None):
        self._get_keystone_conn()
        self._get_vnc_conn()
        if id:
            ks_project = \
                self._ks_project_get(id=id.replace('-', ''))
            display_name = ks_project['name']
            project_id = id
        elif name:
            ks_project = \
                self._ks_project_get(name=name)
            project_id = ks_project['id']
            display_name = name

        domain_uuid = self._ksv3_domain_id_to_uuid(ks_project['domain_id'])
        dom_obj = self._vnc_lib.domain_read(id=domain_uuid)

        # if earlier project exists with same name but diff id,
        # create with uniqified fq_name
        fq_name = dom_obj.get_fq_name() + [display_name]
        try:
            old_id = self._vnc_lib.fq_name_to_id('project', fq_name)
            project_name = '%s-%s' %(display_name, str(uuid.uuid4()))
        except vnc_api.NoIdError:
            project_name = display_name
        
        proj_obj = vnc_api.Project(project_name, parent_obj=dom_obj)
        proj_obj.uuid = project_id
        self._vnc_lib.project_create(proj_obj)
    # end _ksv3_sync_project_to_vnc

    def _ksv3_add_project_to_vnc(self, project_id):
        try:
            self._vnc_lib.project_read(id=project_id)
            # project exists, no-op for now,
            # sync any attr changes in future
        except vnc_api.NoIdError:
            self._ksv3_sync_project_to_vnc(id=project_id)
    # _ksv3_add_project_to_vnc

    def _ksv3_del_project_from_vnc(self, project_id):
        if project_id in self._failed_project_dels:
            return

        try:
            self._vnc_lib.project_delete(id=project_id)
        except vnc_api.NoIdError:
            pass
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())
            self._failed_project_dels.add(project_id)
    # _ksv3_del_project_from_vnc

    def sync_domain_to_vnc(self, domain_id):
        self._get_keystone_conn()
        self._get_vnc_conn()
        ks_domain = \
            self._ks_domain_get(domain_id.replace('-', ''))
        display_name = ks_domain['name']

        # if earlier domain exists with same name but diff id,
        # create with uniqified fq_name
        fq_name = [display_name]
        try:
            old_id = self._vnc_lib.fq_name_to_id('domain', fq_name)
            domain_name = '%s-%s' %(display_name, str(uuid.uuid4()))
        except vnc_api.NoIdError:
            domain_name = display_name
 
        dom_obj = vnc_api.Domain(domain_name)
        dom_obj.uuid = domain_id
        self._vnc_lib.domain_create(dom_obj)
    # sync_domain_to_vnc

    def _add_domain_to_vnc(self, domain_id):
        try:
            self._vnc_lib.domain_read(id=domain_id)
            # domain exists, no-op for now,
            # sync any attr changes in future
        except vnc_api.NoIdError:
            self.sync_domain_to_vnc(domain_id)
    # _add_domain_to_vnc

    def _del_domain_from_vnc(self, domain_id):
        if domain_id in self._failed_domain_dels:
            return

        try:
            self._vnc_lib.domain_delete(id=domain_id)
        except vnc_api.NoIdError:
            pass
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())
            self._failed_domain_dels.add(domain_id)
    # _del_domain_from_vnc

    def _resync_all_domains(self):
        if not self._ks_domains_list:
            # < keystonev3, no domains
            return False

        self._get_keystone_conn()
        # compare new and old set,
        # optimize for common case where nothing has changed,
        # so track the project-ids in a set add '-',
        # keystone gives uuid without...
        try:
            # The Default domain in ks(for v2 support) has id of 'default'
            # replace with uuid of default-domain in vnc
            ks_domain_ids = set(
                [str(uuid.UUID(dom['id']))
                    for dom in self._ks_domains_list() if dom['id'] != 'default'])
            ks_domain_ids.add(self._vnc_default_domain_id)
        except Exception as e:
            self._ks = None
            return True # retry

        vnc_domain_ids = self._vnc_domain_ids
        if vnc_domain_ids == ks_domain_ids:
            # no change, go back to poll
            return False

        for vnc_domain_id in vnc_domain_ids - ks_domain_ids:
            self.q.put((Q_DELETE, 'domain', vnc_domain_id))

        if self._keystone_sync_on_demand:
            # pre_domain_read will get it
            pass
        else:
            for ks_domain_id in ks_domain_ids - vnc_domain_ids:
                self.q.put((Q_CREATE, 'domain', ks_domain_id))

        self.q.join()
        gevent.sleep(0)

        # we are in sync
        self._vnc_domain_ids = ks_domain_ids

        return False
    # end _resync_all_domains

    def _resync_all_projects(self):
        self._get_keystone_conn()
        # compare new and old set,
        # optimize for common case where nothing has changed,
        # so track the project-ids in a set add '-',
        # keystone gives uuid without...
        try:
            ks_project_ids = set(
                [str(uuid.UUID(proj['id']))
                    for proj in self._ks_projects_list()])
        except Exception as e:
            self._ks = None
            return True # retry

        vnc_project_ids = self._vnc_project_ids
        if vnc_project_ids == ks_project_ids:
            # no change, go back to poll
            return False

        for vnc_project_id in vnc_project_ids - ks_project_ids:
            self.q.put((Q_DELETE, 'project', vnc_project_id))

        if self._keystone_sync_on_demand:
            pass # pre_project_read will get it
        else:
            for ks_project_id in ks_project_ids - vnc_project_ids:
                self.q.put((Q_CREATE, 'project', ks_project_id))

        self.q.join()
        gevent.sleep(0)
        # we are in sync
        self._vnc_project_ids = ks_project_ids

        return False
    # end _resync_all_projects

    def _resync_domains_projects_forever(self):
        try:
            # get connection to api-server REST interface
            while True:
                try:
                    self._get_vnc_conn()
                    break
                except requests.ConnectionError:
                    gevent.sleep(1)

            vnc_domains = self._vnc_lib.domains_list()['domains']
            for dom in vnc_domains:
                self._vnc_domain_ids.add(dom['uuid'])
                if dom['fq_name'] == ['default-domain']:
                    self._vnc_default_domain_id = dom['uuid']

            vnc_all_projects = self._vnc_lib.projects_list()['projects']
            # remove default-domain:default-project from audit list
            default_proj_fq_name = ['default-domain', 'default-project']
            vnc_project_ids = set([proj['uuid'] for proj in vnc_all_projects 
                                 if proj['fq_name'] != default_proj_fq_name])
            self._vnc_project_ids = vnc_project_ids
        except Exception as e:
            cgitb.Hook(
                format="text",
                file=open(self._tmp_file_name,
                          'w')).handle(sys.exc_info())
            fhandle = open(self._tmp_file_name)
            self._vnc_os_logger.error("%s" % fhandle.read())

        while True:
            # Get domains/projects from Keystone and audit with api-server
            try:
                retry = self._resync_all_domains()
                if retry:
                    continue
            except Exception as e:
                self._ks = None
                cgitb.Hook(
                    format="text",
                    file=open(self._tmp_file_name,
                              'w')).handle(sys.exc_info())
                fhandle = open(self._tmp_file_name)
                self._vnc_os_logger.error("%s" % fhandle.read())
                gevent.sleep(2)

            try:
                retry = self._resync_all_projects()
                if retry:
                    continue
            except Exception as e:
                self._ks = None
                cgitb.Hook(
                    format="text",
                    file=open(self._tmp_file_name,
                              'w')).handle(sys.exc_info())
                fhandle = open(self._tmp_file_name)
                self._vnc_os_logger.error("%s" % fhandle.read())
                gevent.sleep(2)

            gevent.sleep(self._resync_interval_secs)

        #end while True

    #end _resync_domains_projects_forever

    def resync_domains_projects(self):
        # add asynchronously
        self._main_glet = gevent.spawn(self._resync_domains_projects_forever)
        self._worker_glets = []
        for x in range(self._resync_number_workers):
            self._worker_glets.append(gevent.spawn(self._resync_worker))
    #end resync_domains_projects

    def _resync_worker(self):
        while True:
            oper, obj_type, obj_id = self.q.get()
            try:
                if oper == Q_DELETE:
                    if obj_type == 'domain':
                        self._del_domain_from_vnc(obj_id)
                    elif obj_type == 'project':
                        self._del_project_from_vnc(obj_id)
                    else:
                        raise KeyError("An invalid obj_type was specified: %s",
                                        obj_type)
                elif oper == Q_CREATE:
                    if obj_type == 'domain':
                        self._add_domain_to_vnc(obj_id)
                    elif obj_type == 'project':
                        self._add_project_to_vnc(obj_id)
                    else:
                        raise KeyError("An invalid obj_type was specified: %s",
                                        obj_type)
                else:
                    raise KeyError("An invalid operation was specified: %s", oper)
            except (ValueError, KeyError):
                # For an unpack error or and invalid kind.
                self.log_exception()
            finally:
                self.q.task_done()
    # end _resync_worker

#end class OpenstackResync


class ResourceApiDriver(vnc_plugin_base.ResourceApi):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        if api_server_ip == '0.0.0.0':
            self._vnc_api_ip = '127.0.0.1'
        else:
            self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port
        self._config_sections = conf_sections
        fill_keystone_opts(self, conf_sections)

        self._vnc_lib = None
        self._openstack_drv = OpenstackDriver(api_server_ip, api_server_port, conf_sections)
        # Tracks which domains/projects have been sync'd from keystone to contrail api server
        self._vnc_domains = set()
        self._vnc_projects = set()
        self._conn_glet = gevent.spawn(self._get_api_connection)
    # end __init__

    def _get_api_connection(self):
        if self._vnc_lib:
            return

        # get connection to api-server REST interface
        while True:
            try:
                self._vnc_lib = vnc_api.VncApi(
                    api_server_host=self._vnc_api_ip,
                    api_server_port=self._vnc_api_port,
                    username=self._auth_user,
                    password=self._auth_passwd,
                    tenant_name=self._admin_tenant)

                vnc_lib = self._vnc_lib
                domain_id = vnc_lib.fq_name_to_id(
                        'domain', ['default-domain'])
                project_id = vnc_lib.fq_name_to_id(
                        'project', ['default-domain', 'default-project'])
                self._vnc_projects.add(project_id)
                self._vnc_domains.add(domain_id)
                break
            except requests.ConnectionError:
                gevent.sleep(1)

    def __call__(self):
        pass
    #end __call__

    def _create_default_security_group(self, proj_dict):
        proj_obj = vnc_api.Project.from_dict(**proj_dict)
        sgr_uuid = str(uuid.uuid4())
        ingress_rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                      protocol='any',
                                      src_addresses=[
                                          AddressType(
                                              security_group=proj_obj.get_fq_name_str() + ':' + 'default')],
                                      src_ports=[PortType(0, 65535)],
                                      dst_addresses=[
                                          AddressType(security_group='local')],
                                      dst_ports=[PortType(0, 65535)])
        sg_rules = PolicyEntriesType([ingress_rule])

        sgr_uuid = str(uuid.uuid4())
        egress_rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                     protocol='any',
                                     src_addresses=[
                                         AddressType(security_group='local')],
                                     src_ports=[PortType(0, 65535)],
                                     dst_addresses=[
                                         AddressType(
                                             subnet=SubnetType('0.0.0.0', 0))],
                                     dst_ports=[PortType(0, 65535)])
        sg_rules.add_policy_rule(egress_rule)

        # create security group
        sg_obj = vnc_api.SecurityGroup(name='default', parent_obj=proj_obj,
                                       security_group_entries=sg_rules)

        self._vnc_lib.security_group_create(sg_obj)
    # end _create_default_security_group

    def pre_domain_read(self, id):
        if not self._keystone_sync_on_demand:
            # domain added via poll
            return

        if id in self._vnc_domains:
            return

        try:
            self._openstack_drv.sync_domain_to_vnc(id)
        except vnc_api.RefsExistError as e:
            # another api server has brought syncd it
            pass
        self._vnc_domains.add(id)
    # end pre_domain_read

    def pre_project_read(self, id):
        if not self._keystone_sync_on_demand:
            # project added via poll
            return

        if id in self._vnc_projects:
            return

        try:
            self._openstack_drv.sync_project_to_vnc(id)
        except vnc_api.RefsExistError as e:
            # another api server has brought syncd it
            pass
        self._vnc_projects.add(id)

    # end pre_project_read

    def post_project_create(self, proj_dict):
        self._create_default_security_group(proj_dict)
    # end post_create_project

    def pre_project_delete(self, proj_uuid):
        proj_obj = self._vnc_lib.project_read(id=proj_uuid)
        sec_groups = proj_obj.get_security_groups()
        for group in sec_groups or []:
            if group['to'][2] == 'default':
                self._vnc_lib.security_group_delete(id=group['uuid'])
        self._vnc_projects.remove(proj_uuid)
    # end pre_project_delete

    def pre_virtual_network_create(self, vn_dict):
        pass
    # end pre_virtual_network_create

# end class ResourceApiDriver

class NeutronApiDriver(vnc_plugin_base.NeutronApi):
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._npi = npi.NeutronPluginInterface(api_server_ip, api_server_port,
                                               conf_sections)

        # Bottle callbacks for network operations
        bottle.route('/neutron/network',
                     'POST', self._npi.plugin_http_post_network)

        # Bottle callbacks for subnet operations
        bottle.route('/neutron/subnet',
                     'POST', self._npi.plugin_http_post_subnet)

        # Bottle callbacks for port operations
        bottle.route('/neutron/port',
                     'POST', self._npi.plugin_http_post_port)

        # Bottle callbacks for floating IP operations
        bottle.route('/neutron/floatingip',
                     'POST', self._npi.plugin_http_post_floatingip)

        # Bottle callbacks for security group operations
        bottle.route('/neutron/security_group',
                     'POST', self._npi.plugin_http_post_securitygroup)

        # Bottle callbacks for security group rule operations
        bottle.route('/neutron/security_group_rule',
                     'POST', self._npi.plugin_http_post_securitygrouprule)

        # Bottle callbacks for router operations
        bottle.route('/neutron/router',
                     'POST', self._npi.plugin_http_post_router)

        # Bottle callbacks for ipam operations
        bottle.route('/neutron/ipam',
                     'POST', self._npi.plugin_http_post_ipam)

        # Bottle callbacks for Policy operations
        bottle.route('/neutron/policy',
                     'POST', self._npi.plugin_http_post_policy)

        # Bottle callbacks for route-table operations
        bottle.route('/neutron/route_table',
                     'POST', self._npi.plugin_http_post_route_table)

        # Bottle callbacks for svc-instance operations
        bottle.route('/neutron/nat_instance',
                     'POST', self._npi.plugin_http_post_svc_instance)

    def __call__(self):
        pass

