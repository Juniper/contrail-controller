#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC services for Mesos
"""

# Standard library import
import gevent
from gevent.queue import Empty
import requests

# Application library import
from cfgm_common import importutils
from cfgm_common import vnc_cgitb
from cfgm_common.vnc_amqp import VncAmqpHandle
import mesos_manager.mesos_consts as mesos_consts
from vnc_api.vnc_api import *
from config_db import *
import db
from reaction_map import REACTION_MAP


"""
TODO:
    1. Multiple network create
    2. Add MM caching
    3. Add subnet to network
    4. Change the way fip is done today
    5. Set tag defined subnet type
    6. Fix security group
"""


class VncService(object):
    """"Client to handle vnc api server interactions"""
    def __init__(self, args=None, logger=None):
        self.args = args
        self.logger = logger

        # init vnc connection
        self.vnc_lib = self._vnc_connect()

        # init access to db
        self._db = db.MesosNetworkManagerDB(self.args, self.logger)
        DBBaseMM.init(self, self.logger, self._db)

        # init rabbit connection
        self.rabbit = VncAmqpHandle(self.logger, DBBaseMM, REACTION_MAP,
                                    'mesos_manager', args=self.args)
        self.rabbit.establish()

        # sync api server db in local cache
        self._sync_sm()
        self.rabbit._db_resync_done.set()

    def _vnc_connect(self):
        """Retry till API server connection is up"""
        connected = False
        while not connected:
            try:
                vnc_lib = VncApi(self.args.admin_user,
                                 self.args.admin_password,
                                 self.args.admin_tenant,
                                 self.args.vnc_endpoint_ip,
                                 self.args.vnc_endpoint_port)
                connected = True
                self.logger.info("Connected to API-server {}{}."
                                 .format(self.args.vnc_endpoint_ip,
                                         self.args.vnc_endpoint_port))
            except requests.exceptions.ConnectionError:
                time.sleep(3)
            except ResourceExhaustionError:
                time.sleep(3)
        return vnc_lib

    def _sync_sm(self):
        for cls in DBBaseMM.get_obj_type_map().values():
            for obj in cls.list_obj():
                cls.locate(obj['uuid'], obj)

    @staticmethod
    def reset():
        for cls in DBBaseMM.get_obj_type_map().values():
            cls.reset()

    def add_mesos_task_and_define_network(self, obj_labels):
        """Adds task and network references to VNC db"""
        # Project creation
        vnc_project = VncProject(self.vnc_lib, self.logger)
        project_obj = vnc_project.create_project(obj_labels.domain_name,
                                                 obj_labels.project_name)
        # Network creation
        vnc_network = VncMesosNetwork(self.vnc_lib, self.logger)
        network_obj = vnc_network.create_network(project_obj,
                                                 obj_labels.networks)
        # Register a task
        vnc_task = VncMesosTask(self.vnc_lib, self.logger)
        task_obj = vnc_task.register_task(obj_labels.task_uuid)

        # Create floating-ip, apply security groups, instance-ip and interface
        common_operations = VncMesosCommonOperations(self.vnc_lib, self.logger)
        vmi_obj = common_operations.create_vmi(project_obj, network_obj,
                                               task_obj)
        common_operations.add_security_groups(vmi_obj,
                                              obj_labels.security_groups)
        common_operations.add_floating_ip(vmi_obj, project_obj,
                                          obj_labels.floating_ips,
                                          task_obj.name)
        common_operations.create_instance_ip(task_obj.name, network_obj,
                                             vmi_obj)
        common_operations.link_task_to_vrouter(task_obj,
                                               obj_labels.cluster_name)

    def del_mesos_task_and_remove_network(self, obj_labels):
        """Deletes task and network references to VNC db"""
        task_obj = VirtualMachineMM.get(obj_labels.task_uuid)
        if not task_obj:
            self.logger.error("Delete operation: task entry does not exist")
        else:
            #unlink from vrouter, delete iip,floating ip, security group, vmi
            common_operations = VncMesosCommonOperations(self.vnc_lib,
                                                         self.logger)
            common_operations.unlink_task_from_vrouter(task_obj)
            for vmi_id in list(task_obj.virtual_machine_interfaces):
                vmi_obj = VirtualMachineInterfaceMM.get(vmi_id)
                if vmi_obj:
                    common_operations.remove_instance_ip(vmi_obj)
                    common_operations.remove_floating_ip(vmi_obj)
                    common_operations.remove_security_groups(vmi_obj)
                    common_operations.remove_vmi(vmi_obj)
            # Unregister a task
            vnc_task = VncMesosTask(self.vnc_lib, self.logger)
            task_obj = vnc_task.unregister_task(obj_labels.task_uuid)
            # Remove network
            vnc_network = VncMesosNetwork(self.vnc_lib, self.logger)
            vnc_network.delete_network(obj_labels.task_uuid)


class VncProject(object):
    """VNC project related operations"""
    def __init__(self, vnc_lib, logger):
        self.vnc_lib = vnc_lib
        self.logger = logger

    def create_project(self, domain_name, project_name):
        """Create project in vnc db"""
        proj_fq_name = [domain_name, project_name]
        proj_obj = Project(name=project_name, fq_name=proj_fq_name)
        try:
            self.vnc_lib.project_create(proj_obj)
            self.logger.info('Project object created')
        except RefsExistError:
            proj_obj = self.vnc_lib.project_read(fq_name=proj_fq_name)
            self.logger.info('Project already exist. Reading from VNC')
        ProjectMM.locate(proj_obj.uuid)
        return proj_obj


class VncMesosNetwork(object):
    """Operations relates to network addtion, modification and deletion"""
    def __init__(self, vnc_lib, logger):
        self.vnc_lib = vnc_lib
        self.logger = logger

    def create_network(self, proj_obj, networks):
        """Create network in VNC db"""
        vn_obj = VirtualNetwork(name=networks, parent_obj=proj_obj)
        ipam_obj = self._create_ipam(networks, proj_obj)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))
        try:
            self.vnc_lib.virtual_network_create(vn_obj)
            self.logger.info('Network object created')
        except RefsExistError:
            vn_obj = self.vnc_lib.virtual_network_read(
                                                  fq_name=vn_obj.get_fq_name())
            self.logger.info('Network already exist. Reading from VNC')
        return vn_obj

    def _create_ipam(self, ipam_name, proj_obj):
        """Create ipam for network"""
        ipam_obj = NetworkIpam(name=ipam_name, parent_obj=proj_obj)
        try:
            self.vnc_lib.network_ipam_create(ipam_obj)
        except RefsExistError:
            ipam__obj = self.vnc_lib.network_ipam_read(
                                                fq_name=ipam_obj.get_fq_name())
        return ipam_obj

    def delete_network(self, task_uuid):
        """Remove network from VNC DB"""
        network_obj = VirtualNetworkMM.find_by_name_or_uuid(task_uuid)
        if network_obj:
            self.vnc_lib.virtual_network_delete(id=network_obj.uuid)
            VirtualNetworkMM.delete(network_obj.uuid)


class VncMesosTask(object):
    """Operations related to task registration and deletion"""
    def __init__(self, vnc_lib, logger):
        self.vnc_lib = vnc_lib
        self.logger = logger

    def register_task(self, task_uuid):
        """Insert task info in VNC db"""
        task_obj = VirtualMachine(name=str(task_uuid))
        task_obj.uuid = task_uuid
        try:
            self.vnc_lib.virtual_machine_create(task_obj)
            self.logger.info('Task is registered successfully in VNC db')
        except RefsExistError:
            task_obj = self.vnc_lib.virtual_machine_read(id=task_uuid)
            self.logger.info('Task is already registered')
        VirtualMachineMM.locate(task_obj.uuid)
        return task_obj

    def unregister_task(self, task_uuid):
        """Remove task from VNC DB"""
        try:
            self.vnc_lib.virtual_machine_delete(id=task_uuid)
        except NoIdError:
            pass


class VncMesosCommonOperations(object):
    """Common operation between task and network"""
    def __init__(self, vnc_lib, logger):
        self.vnc_lib = vnc_lib
        self.logger = logger

    def create_vmi(self, project_obj, network_obj, task_obj):
        """Create task interface"""
        vmi_obj = VirtualMachineInterface(name=task_obj.name,
                                          parent_obj=project_obj)
        vmi_obj.set_virtual_network(network_obj)
        vmi_obj.set_virtual_machine(task_obj)
        try:
            self.vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            self.vnc_lib.virtual_machine_interface_update(vmi_obj)
        VirtualMachineInterfaceMM.locate(vmi_obj.uuid)
        return vmi_obj

    def remove_vmi(self, vmi_obj):
        """Remove task interface"""
        try:
            self.vnc_lib.virtual_machine_interface_delete(id=vmi_obj.uuid)
        except NoIdError:
            pass

    def add_security_groups(self, vmi_obj, security_groups):
        """Add security group to interface"""
        if security_groups != "":
            security_group_list = security_groups.split(",")
            for security_group_fq_name in security_group_list:
                security_group_obj = self.vnc_lib.security_group_read(
                                            fq_name_str=security_group_fq_name)
                vmi_obj.add_security_group(security_group_obj)
            self.vnc_lib.virtual_machine_interface_update(vmi_obj)

    def remove_security_groups(self, vmi_obj):
        """Remove security group from  interface"""
        for security_group_uuid in list(vmi_obj.security_groups):
            try:
                self.vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                                        'security-group', security_group_uuid,
                                        None, 'DELETE')
            except Exception as e:
                self.logger.error("Failed to detach SG %s" % str(e))

    def add_floating_ip(self, vmi_obj, project_obj, fip_pool_list, task_name):
        """Create a floating ip"""
        if fip_pool_list != "":
            fip_pool_list = fip_pool_list.split(",")
            for fip_pool_entry in fip_pool_list:
                result = re.split(r'[\(\)]', fip_pool_entry)
                try:
                    fip_pool_name = result[0]
                except IndexError:
                    self.logger.error("Error in processing fip pool string")
                    return
                try:
                    ip_addr = result[1]
                except IndexError:
                    ip_addr = ''
                fip_pool_fq_name = fip_pool_name.split(':')
                try:
                    fip_pool_obj = self.vnc_lib.floating_ip_pool_read(
                                                      fq_name=fip_pool_fq_name)
                except NoIdError, err:
                    self.logger.error("Floating ip pool not found:" + str(err))
                    return
                fip_obj = FloatingIp(name="mesos-fip-{}{}".format(task_name,
                                     ip_addr), parent_obj = fip_pool_obj)
                if ip_addr != "":
                    fip_obj.set_floating_ip_address(ip_addr)
                fip_obj.set_project(project_obj)
                fip_obj.set_virtual_machine_interface(vmi_obj)
                try:
                    self.vnc_lib.floating_ip_create(fip_obj)
                except RefsExistError:
                    self.vnc_lib.floating_ip_update(fip_obj)

    def remove_floating_ip(self, vmi_obj):
        """Remove floating ip"""
        for floating_ip_uuid in list(vmi_obj.floating_ips):
            try:
                self.vnc_lib.floating_ip_delete(id=floating_ip_uuid)
            except NoIdError:
                pass

    def create_instance_ip(self, task_name, vn_obj, vmi_obj):
        """Create insctance ip"""
        iip_obj = InstanceIp(name=task_name)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.add_virtual_machine_interface(vmi_obj)
        try:
            self.vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self.vnc_lib.instance_ip_update(iip_obj)
        InstanceIpMM.locate(iip_obj.uuid)

    def remove_instance_ip(self, vmi_obj):
        """Removal of instance ip"""
        for instance_ip_uuid in list(vmi_obj.instance_ips):
            try:
                self.vnc_lib.instance_ip_delete(id=instance_ip_uuid)
            except NoIdError:
                pass

    def link_task_to_vrouter(self, task_obj, cluster_name):
        """Register a task unto vrouter"""
        vrouter_fq_name = ['default-global-system-config', cluster_name]
        try:
            vrouter_obj = self.vnc_lib.virtual_router_read(
                                                       fq_name=vrouter_fq_name)
        except Exception:
            return

        self.vnc_lib.ref_update('virtual-router', vrouter_obj.uuid,
                                'virtual-machine', task_obj.uuid, None, 'ADD')
        if task_obj:
            task_obj.virtual_router = vrouter_obj.uuid

    def unlink_task_from_vrouter(self, task_obj):
        """Unregister a task from vrouter"""
        if task_obj.virtual_router:
            self.vnc_lib.ref_update('virtual-router', task_obj.virtual_router,
                                    'virtual-machine', task_obj.uuid, None,
                                    'DELETE')


def main():
    """ Code for unit testing"""


if __name__ == '__main__':
    sys.exit(main())
