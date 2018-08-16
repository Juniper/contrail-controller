#!/usr/bin/python

import traceback

import sys
import json
import jsonschema
sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")

from vnc_utils import VncUtils
from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log

class FilterModule(object):

    def filters(self):
        return {
            'topology_discovery': self.topology_discovery,
        }

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("TopologyDiscoveryFilter", device_name)

    def topology_discovery(self, job_ctx, prouter_fqname,
                           lldp_neighbors_payload):

        self._instantiate_filter_log_instance(prouter_fqname[-1])
        _task_log("Starting Topology Discovery")
        try:
            _task_log("Creating neighboring links")
            topology_discovery_resp = self._create_neighbor_links(job_ctx,
                                                                  lldp_neighbors_payload,
                                                                  prouter_fqname)

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

    def get_vnc_payload(self, prouter_fqname, lldp_neighbors_info):

        vnc_payload = []
        for lldp_neighbor_info in lldp_neighbors_info:
            local_phy_int = lldp_neighbor_info['local_physical_interface_name']
            phy_int_fqname = []
            phy_int_fqname.extend(prouter_fqname)
            phy_int_fqname.append(local_phy_int.replace(":", "_"))

            remote_phy_int_fqname_str = "default-global-system-config" +\
                                        ":" + lldp_neighbor_info['remote_device_name'] +\
                                        ":" + lldp_neighbor_info['remote_physical_interface_name'].replace(":", "_")

            vnc_payload.append((phy_int_fqname, remote_phy_int_fqname_str))
        return vnc_payload

    # group vnc functions
    def _create_neighbor_links(self, job_ctx, lldp_neighbors_payload, prouter_fqname):

        if not lldp_neighbors_payload['neighbor_info_list']:
            _task_log("No neighbors found")
            _task_done()
            return {
                    'lldp_neighbors_success_names': [],
                    'lldp_neighbors_failed_info': []
                   }
        vnc_lib = VncUtils._init_vnc_api(job_ctx)

        vnc_topology_disc_payload = self.get_vnc_payload(prouter_fqname, lldp_neighbors_payload['neighbor_info_list'])
        topology_disc_payload = self._do_further_parsing(vnc_lib, vnc_topology_disc_payload)

        _task_done("Parsed payload completely")

        _task_log("Creating links between neighboring physical interfaces")
        topology_discovery_resp = self._create_physical_interface_refs(vnc_lib, topology_disc_payload)
        return topology_discovery_resp

    def _do_further_parsing(self, vnc_lib, neighbor_info_list):
        topology_disc_payload = []
        for neighbor_info in neighbor_info_list:
            remote_neighbor_info = neighbor_info[1].rsplit(":", 1)
            list_resp = vnc_lib.physical_interfaces_list(parent_fq_name=remote_neighbor_info[0].split(":"),
                                                         filters={"physical_interface_port_id": remote_neighbor_info[1]}
                                                        )
            if list_resp['physical-interfaces']:
                topology_disc_payload.append([neighbor_info[0], list_resp['physical-interfaces'][0]['fq_name']])

        return topology_disc_payload

    def _create_physical_interface_refs(self, vnc_lib, topology_disc_payload):
        # create or update refs between physical interfaces
        # on the local device to the remote device
        object_type = "physical_interface"
        ref_type = "physical_interface"
        lldp_neighbors_success_names = []
        lldp_neighbors_failed_info = []

        for topology_disc_info in topology_disc_payload:
            try:
                object_fqname = topology_disc_info[0]
                ref_fqname = topology_disc_info[1]
                object_uuid = vnc_lib.fq_name_to_id(object_type, object_fqname)
                vnc_lib.ref_update(object_type, object_uuid, ref_type, None, ref_fqname, 'ADD')
                lldp_neighbors_success_names.append(object_fqname[-2] + " : " +
                                                    object_fqname[-1] + " --> " +
                                                    ref_fqname[-2] + " : " +
                                                    ref_fqname[-1]
                                                   )
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


