from vnc_utils import VncUtils
from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log
import sys
import traceback

class TopologyDiscoveryBasePlugin(object):
    _device_info_parsers = {}

    def _instantiate_filter_log_instance(self):
        FilterLog.instance("TopologyDiscoveryFilter")

    def register_parser_method(self, vendor, family, method):
        self._device_info_parsers[vendor+"_"+family] = method

    def _get_parser_method(self, vendor, family):
        return self._device_info_parsers[vendor+"_"+family]

    def topology_discovery(self, auth_token, prouter_fqname, prouter_vendor_name,
                           prouter_family_name, device_data):

        topology_disc_payload = []
        self._instantiate_filter_log_instance()
        _task_log("Starting Topology Discovery")
        try:
            _task_log("Obtaining the parser method")
            parser_method = self._get_parser_method(prouter_vendor_name, prouter_family_name)
            _task_done()

            _task_log("Starting to parse device data")
            lldp_neighbors_payload = parser_method(device_data, prouter_fqname)
            _task_done()

            _task_log("Checking to see if payload needs further parsing")
            if not lldp_neighbors_payload['neighbor_info_list']:
                _task_log("No neighbors found")
                _task_done()
                return {
                        'status': 'success',
                        'topology_discovery_log': FilterLog.instance().dump(),
                        'topology_discovery_resp': {'lldp_neighbors_success_names': [],
                                                    'lldp_neighbors_failed_names': []
                                                   }
                       }
            vnc_lib = VncUtils._init_vnc_api(auth_token)
            if lldp_neighbors_payload.get('do_more_parsing') == True:
                topology_disc_payload = self._do_further_parsing(vnc_lib, lldp_neighbors_payload['neighbor_info_list'])
            else:
                topology_disc_payload = lldp_neighbors_payload['neighbor_info_list']
            _task_done("Parsed payload completely")

            _task_log("Creating links between neighboring physical interfaces")
            topology_discovery_resp = self._create_neighbor_links(vnc_lib, topology_disc_payload)
            _task_done()
            return {
                    'status': 'success',
                    'topology_discovery_log': FilterLog.instance().dump(),
                    'topology_discovery_resp': topology_discovery_resp
                   }
        except Exception as ex:
            _task_error_log(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'topology_discovery_log': FilterLog.instance().dump()}

    # group vnc functions

    def _do_further_parsing(self, vnc_lib, neighbor_info_list):
        topology_disc_payload = []
        for neighbor_info in neighbor_info_list:
            remote_neighbor_info = neighbor_info[1].rsplit(":", 1)
            list_resp = vnc_lib.physical_interfaces_list(parent_fq_name=remote_neighbor_info[0],
                                                         filters={"physical_interface_port_id": remote_neighbor_info[1]}
                                                        )
            if list_resp['physical-interfaces']:
                topology_disc_payload.append([neighbor_info[0], list_resp['physical-interfaces'][0]['fq_name']])

        return topology_disc_payload

    def _create_neighbor_links(self, vnc_lib, topology_disc_payload):
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
                traceback.print_exc(file=sys.stdout)
                lldp_neighbors_failed_names.append(object_fqname[-2] + " : " +
                                                   object_fqname[-1] + " --> " +
                                                   ref_fqname[-2] + " : " +
                                                   ref_fqname[-1]
                                                  )
        return {
                 'lldp_neighbors_success_names': lldp_neighbors_success_names,
                 'lldp_neighbors_failed_names': lldp_neighbors_failed_names
               }

