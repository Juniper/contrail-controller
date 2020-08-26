#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from builtins import object
from builtins import str
import json
import os
import uuid

from cfgm_common import PERMS_RWX
from cfgm_common.utils import CamelCase, detailed_traceback, str_to_class
from vnc_api.exceptions import NoIdError, RefsExistError
from vnc_api.gen import resource_client
from vnc_api.gen.resource_client import (
    LogicalRouter,
    NetworkIpam,
    VirtualNetwork
)
from vnc_api.gen.resource_xsd import (
    IpamSubnets,
    IpamSubnetType,
    PermType2,
    SubnetListType,
    SubnetType,
    VirtualNetworkType,
    VnSubnetsType
)


class FabricManager(object):

    _instance = None

    def __init__(self, args, logger, vnc_api):
        FabricManager._instance = self
        self._fabric_ansible_dir = args.fabric_ansible_dir
        self._logger = logger
        self._vnc_api = vnc_api
        self._load_init_data()
    # end __init__

    @classmethod
    def get_instance(cls):
        return cls._instance
    # end get_instance

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if not inst:
            return
        cls._instance = None
    # end destroy_instance

    @classmethod
    def initialize(cls, args, logger, vnc_api):
        if not cls._instance:
            FabricManager(args, logger, vnc_api)
    # end _initialize

    # _create_ipv6_ll_ipam_virtual_network
    # Internal function to create internal virtual network and network ipam
    # for IPV6 link local address.
    def _create_ipv6_ll_ipam_and_vn(self, vnc_api, network_name):
        nw_fq_name = ['default-domain', 'default-project', network_name]
        ipam_fq_name = ['default-domain', 'default-project',
                        '_internal_ipam_ipv6_link_local']
        subnets = VnSubnetsType([(IpamSubnetType(subnet=SubnetType('fe80::',
                                                                   64),
                                                 default_gateway='fe80::1',
                                                 addr_from_start=True,
                                                 subnet_uuid=str(uuid.uuid1())
                                                 )
                                  )]
                                )
        ipam = NetworkIpam(
            name='_internal_ipam_ipv6_link_local',
            fq_name=ipam_fq_name,
            parent_type='project',
            ipam_subnet_method='user-defined-subnet')
        try:
            self._vnc_api.network_ipam_create(ipam)
        except RefsExistError as ex:
            error_msg = 'network IPAM \'ipv6_link_local\' already \
                        exists or other conflict: {}' \
                .format(str(ex))
            self._logger.error(error_msg)
            self._vnc_api.network_ipam_update(ipam)

        network = VirtualNetwork(
            name=network_name,
            fq_name=nw_fq_name,
            parent_type='project',
            address_allocation_mode="user-defined-subnet-only")
        try:
            network.add_network_ipam(ipam, subnets)
            vnc_api.virtual_network_create(network)
        except Exception as ex:
            self._logger.error("virtual network '%s' already exists or "
                               "other conflict: %s" % (network_name, str(ex)))
            vnc_api.virtual_network_update(network)
    # end _create_ipv6_ll_ipam_and_vn

    # Load init data for job playbooks like JobTemplates, Tags, etc
    def _load_init_data(self):
        """
        Load init data for job playbooks.

        This function loads init data from a data file specified by the
        argument '--fabric_ansible_dir' to the database. The data file
        must be in JSON format and follow the format below

        "my payload": {
            "object_type": "tag"
            "objects": [
                {
                    "fq_name": [
                        "fabric=management_ip"

                    ],
                    "name": "fabric=management_ip",
                    "tag_type_name": "fabric",
                    "tag_value": "management_ip"

                }

            ]

        }
        """
        try:
            json_data = self._load_json_data()
            if json_data is None:
                self._logger.error('Unable to load init data')
                return

            for item in json_data.get("data"):
                object_type = item.get("object_type")

                # Get the class name from object type
                cls_name = CamelCase(object_type)
                # Get the class object
                cls_ob = str_to_class(cls_name, resource_client.__name__)

                # saving the objects to the database
                for obj in item.get("objects"):
                    instance_obj = cls_ob.from_dict(**obj)

                    # create/update the object
                    fq_name = instance_obj.get_fq_name()
                    try:
                        uuid_id = self._vnc_api.fq_name_to_id(
                            object_type, fq_name)
                        if object_type == "tag":
                            continue
                        instance_obj.set_uuid(uuid_id)
                        # Update config json inside role-config object
                        if object_type == 'role-config':
                            role_config_obj = self._vnc_api.\
                                role_config_read(id=uuid_id)
                            cur_config_json = json.loads(
                                role_config_obj.get_role_config_config())
                            def_config_json = json.loads(
                                instance_obj.get_role_config_config())
                            def_config_json.update(cur_config_json)
                            instance_obj.set_role_config_config(
                                json.dumps(def_config_json)
                            )

                        if object_type != 'telemetry-profile' and \
                                object_type != 'sflow-profile' and \
                                object_type != 'device-functional-group':
                            self._vnc_api._object_update(object_type,
                                                         instance_obj)

                    except NoIdError:
                        self._vnc_api._object_create(object_type, instance_obj)

            for item in json_data.get("refs"):
                from_type = item.get("from_type")
                from_fq_name = item.get("from_fq_name")
                from_uuid = self._vnc_api.fq_name_to_id(
                    from_type, from_fq_name)

                to_type = item.get("to_type")
                to_fq_name = item.get("to_fq_name")
                to_uuid = self._vnc_api.fq_name_to_id(to_type, to_fq_name)

                self._vnc_api.ref_update(from_type, from_uuid, to_type,
                                         to_uuid, to_fq_name, 'ADD')
        except Exception as e:
            err_msg = 'error while loading init data: %s\n' % str(e)
            err_msg += detailed_traceback()
            self._logger.error(err_msg)

        # create VN and IPAM for IPV6 link-local addresses
        ipv6_link_local_nw = '_internal_vn_ipv6_link_local'
        self._create_ipv6_ll_ipam_and_vn(self._vnc_api,
                                         ipv6_link_local_nw
                                         )

        # - fetch list of all the physical routers
        # - check physical and overlay role associated with each PR
        # - create ref between physical_role and physical_router object,
        # if PR is assigned with a specific physical role
        # - create ref between overlay_roles and physical_router object,
        # if PR is assigned with specific overlay roles
        obj_list = self._vnc_api._objects_list('physical-router')
        pr_list = obj_list.get('physical-routers')
        for pr in pr_list or []:
            try:
                pr_obj = self._vnc_api.\
                    physical_router_read(id=pr.get('uuid'))
                physical_role = pr_obj.get_physical_router_role()
                overlay_roles = pr_obj.get_routing_bridging_roles()
                if overlay_roles is not None:
                    overlay_roles = overlay_roles.get_rb_roles()
                if physical_role:
                    try:
                        physical_role_uuid = self._vnc_api.\
                            fq_name_to_id('physical_role',
                                          ['default-global-system-config',
                                           physical_role])
                        if physical_role_uuid:
                            self._vnc_api.ref_update('physical-router',
                                                     pr.get('uuid'),
                                                     'physical-role',
                                                     physical_role_uuid,
                                                     None, 'ADD')
                    except NoIdError:
                        pass
                if overlay_roles:
                    for overlay_role in overlay_roles or []:
                        try:
                            overlay_role_uuid = self._vnc_api.\
                                fq_name_to_id('overlay_role',
                                              ['default-global-system-config',
                                               overlay_role.lower()])
                            if overlay_role_uuid:
                                self._vnc_api.ref_update('physical-router',
                                                         pr.get('uuid'),
                                                         'overlay-role',
                                                         overlay_role_uuid,
                                                         None, 'ADD')
                        except NoIdError:
                            pass
            except NoIdError:
                pass

        # handle replacing master-LR as <fab_name>-master-LR here
        # as part of in-place cluster update. Copy the master-LR
        # and also its associated vns and their annotations here

        master_lr_obj = None
        try:
            master_lr_obj = self._vnc_api.logical_router_read(
                fq_name=[
                    'default-domain', 'default-project', 'master-LR'
                ]
            )
        except NoIdError:
            try:
                master_lr_obj = self._vnc_api.logical_router_read(
                    fq_name=[
                        'default-domain', 'admin', 'master-LR'
                    ]
                )
            except NoIdError:
                pass

        if master_lr_obj:
            vmi_refs = master_lr_obj.get_virtual_machine_interface_refs(
            ) or []
            # get existing pr refs
            pr_refs = master_lr_obj.get_physical_router_refs() or []
            fabric_refs = master_lr_obj.get_fabric_refs() or []
            perms2 = master_lr_obj.get_perms2()
            fab_fq_name = None

            try:
                # This has to happen before creating fab-master-LR as
                # otherwise it will fail creation
                # of fab-master-lr with annotations having master-lr uuid
                # Now delete master-LR object
                # this will delete lr annotations from fabric in
                # corresponding VNs if they exist
                self._vnc_api.logical_router_delete(
                    id=master_lr_obj.get_uuid())

                # try to obtain the fabric refs either by fabric ref if one
                # is available or from pr_refs if available

                if pr_refs and not fabric_refs:
                    # this is assuming that even though there can be
                    # multiple pr refs, a LR cannot have more than
                    # one fabric refs. So a random pr chosen in the pr
                    # refs list will have the same fabric name as the other
                    # prs in the list
                    pr_ref = pr_refs[-1]
                    pr_obj = self._vnc_api.physical_router_read(
                        id=pr_ref.get('uuid',
                                      self._vnc_api.fq_name_to_id(
                                          pr_ref.get('to')
                                      )))
                    fabric_refs = pr_obj.get_fabric_refs() or []

                if fabric_refs:
                    fabric_ref = fabric_refs[-1]
                    fab_fq_name = fabric_ref.get(
                        'to', self._vnc_api.id_to_fq_name(
                            fabric_ref.get('uuid')))

                # if fab_fq_name is not derivable or was not present, then
                # skip creating fab_name-master-LR as fabric information
                # is not available
                # if fab_fq_name is available, copy necessary refs from prev.
                # master LR, create new fab_name-master-LR and this will update
                # VN annotations accordingly.
                if fab_fq_name:
                    self._create_fabric_master_LR(fab_fq_name,
                                                  perms2,
                                                  fabric_refs=fabric_refs,
                                                  vmi_refs=vmi_refs,
                                                  pr_refs=pr_refs)
            except NoIdError:
                pass
            except Exception as exc:
                err_msg = "An exception occurred while attempting to " \
                          "create fabric master-LR: %s " % exc.message
                self._logger.warning(err_msg)

        # Handle fabric master LR creation for all fabrics here
        # iterate through all existing fabric objects
        # create fabric master LR for all existing fabrics

        obj_list = self._vnc_api._objects_list('fabric')
        fab_list = obj_list.get('fabrics')
        for fabric in fab_list or []:
            try:
                fab_obj = self._vnc_api. \
                    fabric_read(id=fabric.get('uuid'))
                fab_fq_name = fab_obj.get_fq_name()

                # check if not default-fabric, if yes
                # skip creating fabric master-LR for it

                if fab_fq_name == ["default-global-system-config",
                                   "default-fabric"]:
                    continue

                perms2 = PermType2('cloud-admin', PERMS_RWX, PERMS_RWX, [])
                # create fabric master LR
                self._create_fabric_master_LR(fab_fq_name,
                                              perms2,
                                              fabric_obj=fab_obj)
            except RefsExistError:
                pass
            except Exception as exc:
                err_msg = "An exception occurred while attempting to " \
                          "create fabric master-LR: %s " % exc.message
                self._logger.warning(err_msg)

        # handle deleted job_templates as part of in-place cluster update
        to_be_del_jt_names = ['show_interfaces_template',
                              'show_config_interfaces_template',
                              'show_interfaces_by_names_template']
        for jt_name in to_be_del_jt_names:
            try:
                self._vnc_api.job_template_delete(
                    fq_name=['default-global-system-config', jt_name])
            except NoIdError:
                pass

    # end _load_init_data

    # create default fabric master LR
    def _create_fabric_master_LR(self, fab_fq_name,
                                 perms2, fabric_refs=None,
                                 fabric_obj=None,
                                 vmi_refs=None,
                                 pr_refs=None):

        def_project = self._vnc_api.project_read(
            ['default-domain', 'default-project'])
        fab_name = fab_fq_name[-1]
        lr_fq_name = ['default-domain',
                      'default-project',
                      fab_name + '-master-LR']
        fab_master_lr_obj = LogicalRouter(
            name=lr_fq_name[-1],
            fq_name=lr_fq_name,
            logical_router_gateway_external=False,
            logical_router_type='vxlan-routing',
            parent_obj=def_project
        )
        perms2.set_global_access(PERMS_RWX)
        fab_master_lr_obj.set_perms2(perms2)

        if vmi_refs:
            fab_master_lr_obj.set_virtual_machine_interface_list(
                vmi_refs)
        if pr_refs:
            fab_master_lr_obj.set_physical_router_list(pr_refs)

        if fabric_refs:
            fab_master_lr_obj.set_fabric_list(fabric_refs)

        if fabric_obj:
            fab_master_lr_obj.set_fabric(fabric_obj)

        self._vnc_api.logical_router_create(
            fab_master_lr_obj
        )
    # end _create_fabric_master_LR

    # Load json data from fabric_ansible_playbooks/conf directory
    def _load_json_data(self):
        json_file = self._fabric_ansible_dir + '/conf/predef_payloads.json'
        if not os.path.exists(json_file):
            msg = 'predef payloads file does not exist: %s' % json_file
            self._logger.error(msg)
            return None

        # open the json file
        with open(json_file) as data_file:
            input_json = json.load(data_file)

        # Loop through the json
        for item in input_json.get("data"):
            if item.get("object_type") == "job-template":
                for obj in item.get("objects"):
                    fq_name = obj.get("fq_name")[-1]
                    schema_name = fq_name.replace('template', 'schema.json')
                    with open(os.path.join(self._fabric_ansible_dir +
                              '/schema/', schema_name), 'r+') as schema_file:
                        schema_json = json.load(schema_file)
                        obj["job_template_input_schema"] = json.dumps(
                            schema_json.get("input_schema"))
                        obj["job_template_output_schema"] = json.dumps(
                            schema_json.get("output_schema"))
                        obj["job_template_input_ui_schema"] = json.dumps(
                            schema_json.get("input_ui_schema"))
                        obj["job_template_output_ui_schema"] = json.dumps(
                            schema_json.get("output_ui_schema"))
        return input_json
    # end _load_json_data

# end FabricManager
