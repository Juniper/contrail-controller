#!/usr/bin/python

from builtins import object
from builtins import str
import sys
import traceback

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import _task_done, _task_error_log, _task_log, FilterLog

from job_manager.job_utils import JobVncApi


class FilterModule(object):

    def filters(self):
        return {
            'import_lldp_info': self.import_lldp_info,
        }
    # end filters

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("Import_lldp_info_Filter", device_name)
    # end _instantiate_filter_log_instance

    def import_lldp_info(self, job_ctx, prouter_fqname,
                         prouter_vendor,
                         lldp_neighbors_payload):
        """Topology discovery.

        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "device_auth": [{
                        "username": "root",
                        "password": "Embe1mpls"
                    }],
                    "management_subnets": [
                        {
                            "cidr": "10.87.69.0/25",
                            "gateway": "10.87.69.1"
                        }
                    ],
                    "overlay_ibgp_asn": 64512,
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5k"
                        }
                    ]
                }
            }
        :param prouter_fqname: List
            example:
            [
              "default-global-system-config",
              "5c3-qfx2"
            ]
        :param prouter_vendor: String
            example: "juniper"
        :param lldp_neighbors_payload: Dictionary
            example:
            {
              "neighbor_info_list":
              [
                {
                  "local_physical_interface_name": "xe-0/0/0",
                  "remote_device_name": "5b5-qfx11",
                  "remote_physical_interface_port_id": "536"
                },
                {
                  "local_physical_interface_name": "xe-0/0/2",
                  "remote_device_chassis_id": "00:1a:53:46:7b:9e",
                  "remote_physical_interface_port_id": "538"
                }
              ]
            }
        :return: Dictionary
        if success, returns
            {
              'status': 'success',
              'topology_discovery_log':
                  <String: topology_discovery_log>,
              'topology_discovery_resp':
                  <Dictionary: topology_discovery_resp>
            }
        if failure, returns
            {
              'status': 'failure',
              'error_msg': <String: exception message>,
              'topology_discovery_log':
                  <String: topology_discovery_log>,
              'topology_discovery_resp':
                  <Dictionary: topology_discovery_resp>
            }
        :param topology_discovery_resp: Dictionary
            example:
            {
              "lldp_neighbors_success_names":
                  <List: <String: lldp_neighbors_success_pair_string>>,
              "lldp_neighbors_failed_info":
                  <List: <Dictionary: lldp_neighbor_failed_obj> >
            }
            :param lldp_neighbors_success_names: List
            example:
                ["bng-contrail-qfx51-15 : ge-0/0/36 --> dhawan : ge-2/3/1"]
            :param lldp_neighbors_failed_info: List
            example:
                [
                  {
                    "lldp_neighbor":
                        "bng-contrail-qfx51-15 : em0 --> sw174 : ge-1/0/46",
                    "warning_message":
                        "Unknown physical interface ng-contrail-qfx51-15:em0"
                  }
                ]
        """
        self._instantiate_filter_log_instance(prouter_fqname[-1])
        _task_log("Starting Topology Discovery")
        try:
            _task_log("Creating neighboring links")
            topology_discovery_resp = self._create_neighbor_links(
                job_ctx,
                lldp_neighbors_payload,
                prouter_fqname,
                prouter_vendor)

            _task_done()
            return {
                'status': 'success',
                'topology_discovery_log': FilterLog.instance().dump(),
                'topology_discovery_resp': topology_discovery_resp
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'topology_discovery_log': FilterLog.instance().dump()}
    # end import_lldp_info

    def get_vnc_payload(self, vnc_lib, prouter_fqname,
                        prouter_vendor,
                        lldp_neighbors_info):

        vnc_payload = []
        chassis_id_device_name_map = self.get_chassis_id_to_device_name(
            vnc_lib, prouter_vendor)
        for lldp_neighbor_info in lldp_neighbors_info or []:
            local_phy_int = lldp_neighbor_info.get(
                'local_physical_interface_name')
            phy_int_fqname = []
            phy_int_fqname.extend(prouter_fqname)
            phy_int_fqname.append(local_phy_int.replace(":", "_"))

            remote_device_chassis_id = lldp_neighbor_info.get(
                'remote_device_chassis_id')
            remote_device_name = chassis_id_device_name_map.get(
                remote_device_chassis_id)

            if not remote_device_name:
                remote_device_name = lldp_neighbor_info.get(
                    'remote_device_name')

            if remote_device_name:
                remote_phy_int_fqname_str = \
                    remote_device_name.replace(
                        ":", "_") + ":" +\
                    lldp_neighbor_info.get(
                        'remote_physical_interface_port_id')

                vnc_payload.append((phy_int_fqname, remote_phy_int_fqname_str))
        return vnc_payload
    # end get_vnc_payload

    # get chassis mac id to physical router name map
    # for all the physical routers in the fabric

    def get_chassis_id_to_device_name(self, vnc_lib, prouter_vendor):

        chassis_id_to_device_name_map = {}

        phy_routers_list = vnc_lib.physical_routers_list(
            fields=['device_chassis_refs']).get('physical-routers')
        for phy_router in phy_routers_list or []:
            if phy_router.get('device_chassis_refs'):
                device_chassis_id_info = phy_router.get(
                    'device_chassis_refs')
                for chassis_id_info in device_chassis_id_info or []:
                    chassis_mac = chassis_id_info['to'][-1].split(
                        prouter_vendor + '_')[1].replace('_', ':')
                    chassis_id_to_device_name_map[chassis_mac] = \
                        phy_router['fq_name'][-1]

        return chassis_id_to_device_name_map
    # end get_chassis_id_to_device_name

    # group vnc functions
    def _create_neighbor_links(self, job_ctx,
                               lldp_neighbors_payload,
                               prouter_fqname,
                               prouter_vendor):

        if not lldp_neighbors_payload.get('neighbor_info_list'):
            _task_log("No neighbors found")
            _task_done()
            return {
                'lldp_neighbors_success_names': [],
                'lldp_neighbors_failed_info': []
            }
        vnc_lib = JobVncApi.vnc_init(job_ctx)

        vnc_topology_disc_payload = self.get_vnc_payload(
            vnc_lib,
            prouter_fqname,
            prouter_vendor,
            lldp_neighbors_payload['neighbor_info_list'])
        topology_disc_payload = self._do_further_parsing(
            vnc_lib, vnc_topology_disc_payload)

        _task_done("Parsed payload completely")

        _task_log("Creating links between neighboring physical interfaces")
        topology_discovery_resp = self._create_physical_interface_refs(
            vnc_lib, topology_disc_payload)
        return topology_discovery_resp
    # end _create_neighbor_links

    def _do_further_parsing(self, vnc_lib, neighbor_info_list):
        topology_disc_payload = []
        for neighbor_info in neighbor_info_list or []:
            remote_neighbor_info = neighbor_info[1].split(":", 1)
            list_resp = vnc_lib.physical_interfaces_list(
                parent_fq_name=["default-global-system-config",
                                remote_neighbor_info[0]],
                filters={"physical_interface_port_id":
                         remote_neighbor_info[1]}
            )
            if list_resp['physical-interfaces']:
                topology_disc_payload.append([neighbor_info[0],
                                              list_resp['physical-interfaces']
                                              [0]['fq_name']])

        return topology_disc_payload
    # end _do_further_parsing

    def _create_physical_interface_refs(self, vnc_lib, topology_disc_payload):
        # create or update refs between physical interfaces
        # on the local device to the remote device
        object_type = "physical_interface"
        ref_type = object_type
        lldp_neighbors_success_names = []
        lldp_neighbors_failed_info = []

        # remove any stale PI refs if any, resultant of a failed
        # fabric onboarding or any other related workflow

        for topology_disc_info in topology_disc_payload or []:
            try:
                object_fqname = topology_disc_info[0]
                pi_obj = vnc_lib.physical_interface_read(
                    fq_name=object_fqname)
                pi_obj.set_physical_interface_list([])
                vnc_lib.physical_interface_update(pi_obj)
            except Exception as ex:
                _task_error_log(str(ex))
                _task_error_log(traceback.format_exc())
                lldp_neighbor_failed_obj = {
                    "lldp_neighbor": object_fqname[-2] + " : " +
                    object_fqname[-1],
                    "warning_message": str(ex)
                }
                lldp_neighbors_failed_info.append(lldp_neighbor_failed_obj)

        for topology_disc_info in topology_disc_payload or []:
            try:
                object_fqname = topology_disc_info[0]
                ref_fqname = topology_disc_info[1]
                object_uuid = vnc_lib.fq_name_to_id(object_type, object_fqname)
                vnc_lib.ref_update(object_type, object_uuid,
                                   ref_type, None, ref_fqname, 'ADD')
                lldp_neighbors_success_names.append(object_fqname[-2] + " : " +
                                                    object_fqname[-1] +
                                                    " --> " +
                                                    ref_fqname[-2] + " : " +
                                                    ref_fqname[-1])
            except Exception as ex:
                _task_error_log(str(ex))
                _task_error_log(traceback.format_exc())
                lldp_neighbor_failed_obj = {
                    "lldp_neighbor": object_fqname[-2] + " : " +
                    object_fqname[-1] + " --> " +
                    ref_fqname[-2] + " : " +
                    ref_fqname[-1],
                    "warning_message": str(ex)
                }
                lldp_neighbors_failed_info.append(lldp_neighbor_failed_obj)
        return {
            'lldp_neighbors_success_names': lldp_neighbors_success_names,
            'lldp_neighbors_failed_info': lldp_neighbors_failed_info
        }
    # end _create_physical_interface_refs
