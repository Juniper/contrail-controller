#!/usr/bin/python

import traceback

import sys
sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
sys.path.append("/opt/contrail/fabric_ansible_playbooks/device_parsers")

from vnc_utils import VncUtils
from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log, validate_payload

class FilterModule(object):

    def filters(self):
        return {
            'topology_discovery': self.topology_discovery,
        }

    def _instantiate_filter_log_instance(self):
        FilterLog.instance("TopologyDiscoveryFilter")

    def topology_discovery(self, auth_token, prouter_fqname, prouter_vendor_name,
                           prouter_family_name, device_data):

        topology_disc_payload = []
        self._instantiate_filter_log_instance()
        _task_log("Starting Topology Discovery")
        try:
            _task_log("Obtaining the parser method and parsing the data")
            file_module = __import__(prouter_vendor_name + "_topology_discovery")
            class_name = getattr(file_module, "ParserModule")
            class_instance = class_name()
            prouter_vendor_name = prouter_vendor_name.replace("-", "_")
            prouter_family_name = prouter_family_name.replace("-", "_")
            lldp_neighbors_payload = getattr(class_instance, 'parse_%s_%s' %(
                                    prouter_vendor_name,
                                    prouter_family_name))(device_data,
                                                          prouter_fqname)

            _task_done()

            _task_log("Creating neighboring links")
            topology_discovery_resp = self._create_neighbor_links(auth_token,
                                                                  lldp_neighbors_payload,
                                                                  self.validate_lldp_neighbors_payload)

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

    def validate_lldp_neighbors_payload(self, payload):
        if 'neighbor_info_list' not in payload:
            raise AttributeError("KeyError: 'neighbor_info_list' key must be in parsed output")
 
    # group vnc functions
    @validate_payload
    def _create_neighbor_links(self, auth_token, lldp_neighbors_payload, validator_method):
        _task_log("Checking to see if payload needs further parsing")
        if not lldp_neighbors_payload['neighbor_info_list']:
            _task_log("No neighbors found")
            _task_done()
            return {
                    'lldp_neighbors_success_names': [],
                    'lldp_neighbors_failed_names': []
                   }
        vnc_lib = VncUtils._init_vnc_api(auth_token)
        if lldp_neighbors_payload.get('do_more_parsing') == True:
            topology_disc_payload = self._do_further_parsing(vnc_lib, lldp_neighbors_payload['neighbor_info_list'])
        else:
            topology_disc_payload = lldp_neighbors_payload['neighbor_info_list']
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
        lldp_neighbors_failed_names = []

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
                lldp_neighbors_failed_names.append(object_fqname[-2] + " : " +
                                                   object_fqname[-1] + " --> " +
                                                   ref_fqname[-2] + " : " +
                                                   ref_fqname[-1]
                                                  )
        return {
                 'lldp_neighbors_success_names': lldp_neighbors_success_names,
                 'lldp_neighbors_failed_names': lldp_neighbors_failed_names
               }


