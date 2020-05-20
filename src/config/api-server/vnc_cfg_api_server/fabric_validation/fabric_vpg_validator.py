from vnc_api import vnc_api
import argparse
import sys
import logging
import os
import calendar
import time
import socket
import json
import decimal
if __name__ == '__main__' and __package__ is None:
    parent = os.path.abspath(os.path.dirname(__file__))
    try:
        sys.path.remove(str(parent))
    except ValueError:  # Already removed
        pass
    import vnc_cfg_api_server  # noqa
    __package__ = 'vnc_cfg_api_server'  # noqa

from . import utils  # noqa


__version__ = "1.0"
SPLIT_SIZE=2
UNTAGGED_ERROR_NUM=2
"""
NOTE: As that script is not self contained in a python package and as it
supports multiple Contrail releases, it brings its own version that needs to be
manually updated each time it is modified. We also maintain a change log list
in that header:
* 1.0:
    - Script checks if Virtual Port Groups with TF violate any 
      VN/VLAN restrictions.
"""

# Class handles Validation of VPGs with Fabric
class FabricVPGValidator(object):
    def __init__(self, args=''):
        self._args = args
        
        # Support Logging
        self._logger = utils.ColorLog(logging.getLogger(__name__))
        log_level = 'DEBUG' if self._args.debug else 'INFO'
        self._logger.setLevel(log_level)
        logformat = logging.Formatter("%(levelname)s: %(message)s")
        stdout = logging.StreamHandler(sys.stdout)
        stdout.setFormatter(logformat)
        self._logger.addHandler(stdout)
        logfile = logging.handlers.RotatingFileHandler(
            self._args.log_file, maxBytes=10000000, backupCount=5)
        logfile.setFormatter(logformat)
        self._logger.addHandler(logfile)
        
        # Initiate Validation required parameters
        hostname = socket.gethostname()
        self.vnc_lib = vnc_api.VncApi(
                api_server_host=socket.gethostbyname(hostname))
        self.vpg_uuids = self.vnc_lib.virtual_port_groups_list(
                fields=['annotations'])
        self.validation_failures = {}

    def _get_vpg_obj_dicts(self):
        return self.vpg_uuids['virtual-port-groups']

    def _get_vnc_lib(self):
        return self.vnc_lib
    
    # Function returns all annotations for a given VPG as a list
    def _get_annotations_for_vpg(self, vpg_dict):
        annotations_list = []
        annotations_kv_pairs = vpg_dict['annotations']['key_value_pair']
        for annotations_kv_dict in annotations_kv_pairs:
            annotation_key = annotations_kv_dict['key']
            annotations_value = annotations_kv_dict['value']
            annotations_list.append((annotation_key, annotations_value))
        return annotations_list
    
    # Helper function that returns info within annotation as a dictionary
    def _extract_annotation_info(self, annotation, vmi_val):
        untagged = False
        annotation_info_dict = {}
        if 'untagged_vlan_id' in annotation:
            untagged = True
            value_split = vm_val.split(":")
            annotation_info_dict['untagged_vlan'] = value_split[0]    
        annotation_data = annotation.split('/')
        for ann_info in annotation_data:
            tag_val = ann_info.split(':')
            if len(tag_val) == SPLIT_SIZE:
                annotation_info_dict[tag_val[0]] = tag_val[1]
        return annotation_info_dict, untagged
    
    # Function that finds validation Errors within VPG
    def _validation_check_within_vpg(self, vpg_dict, fabric_vn_vlan_set):
        local_vn_set = set()
        local_vlan_set = set()
        local_vn_vlan_set = set()
        untagged_vlan = None
        annotations_seen_map = {}
        vpg_uuid = vpg_dict['uuid']
        local_validation_failures = []
        across_fabric_failures = []
        untagged_fabric_failures = []
        
        untagged = False
        # Iterate over all annotations with VPG
        for annotation, vmi_uuid in self._get_annotations_for_vpg(vpg_dict):
            annotation_info_dict, untagged = \
                self._extract_annotation_info(annotation, vmi_uuid)
            # check for untagged vlans and ensure that no more than one 
            # untagged vlan is used
            if untagged == True:
                if untagged_vlan == None: 
                    untagged_vlan = annotation_info_dict['untagged_vlan']
                if annotation_info_dict['untagged_vlan'] != untagged_vlan: 
                    untagged_fabric_failures.append((annotation, vmi_uuid, 
                        "Duplicate Untagged VLAN {0}".format(
                        annotation_info_dict['untagged_vlan'])))
                    continue
           
            try:
                vn_uuid = annotation_info_dict['vn'] 
                vlan_id = annotation_info_dict['vlan_id']
            except KeyError:
                self._logger.error(
                        "Needed arguments are missing from Annotation") 
            
            # Check for inconsistencies with VPG
            if annotation not in annotations_seen_map:
                annotations_seen_map[annotation] = vmi_uuid
                local_vn_vlan_set.add((vn_uuid, vlan_id))
            else:
                local_validation_failures.append((annotation, vmi_uuid, 
                    "VMI currently already using VN/VMI combo".format(
                        annotations_seen_map[annotation])))
                continue

            # Checks needed for only enterprise style fabrics
            if annotation_info_dict['validation'] == 'enterprise':
                if vn_uuid not in local_vn_set:
                    local_vn_set.add(vn_uuid)
                else:
                    self._logger.debug(
                        "VN with uuid {0} already in use by a different VMI".
                        format(vn_uuid))
                    local_validation_failures.append(
                        (annotation, vmi_uuid, 
                         "Offending VN {0}".format(vn_uuid)))
                    continue
                if vlan_id not in local_vlan_set:
                    local_vlan_set.add(vlan_id)
                else:
                    self._logger.debug(
                        "VLAN  with uuid {0} already in use by a different VMI"
                        .format(vlann_id))
                    local_validation_failures.append((annotation, vmi_uuid, 
                        "Offending VLAN {0}".format(vlan_id)))
                    continue
            
                # finally check across vpgs
                if (vn_uuid, vlan_id) in fabric_vn_vlan_set:
                    continue
                else:
                    self._logger.debug("VN/VLAN combination {0} not exact in".
                            format((vn_uuid, vlan_id)) + " different VPG")
                    across_fabric_failures.append((annotation, vmi_uuid, 
                                (vn_uuid, vlan_id)))
        fabric_vn_vlan_set = fabric_vn_vlan_set.union(local_vn_vlan_set)
        return vpg_uuid, local_validation_failures, untagged_fabric_failures, \
            across_fabric_failures, fabric_vn_vlan_set 
    
    # Loop over all vpgs with Fabric and check for errors
    def _validation_check_within_fabric(self):
        fabric_vn_vlan_set = set()
        self._num_total_vpgs = len(self._get_vpg_obj_dicts())
        self.across_fabric_errors = 0
        self.within_vpg_errors = 0
        self.untagged_vlan_errors = 0
        self.invalid_vpgs = 0
        for vpg_dict in self._get_vpg_obj_dicts():
            vpg_uuid, local_validation_failures, untagged_failures, \
                across_fabric_failures, fabric_vn_vlan_set = \
            self._validation_check_within_vpg(vpg_dict, fabric_vn_vlan_set)
            self.across_fabric_errors += len(across_fabric_failures)
            self.untagged_vlan_errors += len(untagged_failures)
            self.within_vpg_errors += len(local_validation_failures)

            self.validation_failures[vpg_uuid] = {
                'local_check' : local_validation_failures,
                'untagged_vlan' : untagged_failures,
                'across_fabric' : across_fabric_failures
            }
        
        self.total_errors = self.across_fabric_errors + \
                            self.untagged_vlan_errors + \
                            self.within_vpg_errors
    
    def _report_within_vpg_errors(self, local_errors):
        if len(local_errors) == 0:
            return
        self._logger.info(
            "Validation Errors that occured in VPG due to wrong combination:")
        for annotation, vmi_uuid, offending_vmi_uuid  in local_errors:
            self._logger.info(
                "Invalid VN/VLAN combination {0} for VMI: {1} due to existing VMI {2}".
                format(annotation, vmi_uuid, offending_vmi_uuid))
    
    def _report_across_fabric_errors(self, local_errors):
        if len(local_errors) == 0:
            return
        self._logger.info(
            "Validation Errors that occured due to other VPGs within fabric ")
        for annotation, vmi_uuid, vlan_vn  in local_errors:
            self._logger.info(
                "Invalid VN/VLAN combination " +
                "{0} for VMI: {1} due to existting VN or VLAN in different VPG {2}"
                .format(annotation, vmi_uuid, vlan_vn))
    
    def _report_untagged_vlan_errors(self, local_errors):
        if len(local_errors) == 0:
            return
        self._logger.info("Validation Errors that occured due to multiple Untagged VLANs")
        for annotation, vmi_uuid, offending_vmi_uuid  in local_errors:
            self._logger.info(
                "Invalid system change {0} for VMI: {1} due to existing VMI {2}"
                .format(annotation, vmi_uuid, offending_vmi_uuid))
    
    def _report_validation_error_in_fabric(self):
        for vpg, errors in self.validation_failures.items():
            if all(len(error) == 0 for error_type, error in errors.items()):
                self._logger.info(
                    "For vpg with uuid: {0}, there were no failures".format(vpg))
            else:
                self.invalid_vpgs += 1
                self._logger.info(
                        "The following errors occured for vpg with uuid: {0}"
                        .format(vpg))
                self._report_within_vpg_errors(errors['local_check'])
                self._report_across_fabric_errors(errors['across_fabric'])
                self._report_untagged_vlan_errors(errors['untagged_vlan'])

    def _report_statistics(self):
        print("\n")
        print("Reporting Statistics:")
        across_percent = 1.0 * (self.across_fabric_errors / self.total_errors) * 100
        within_vpg = 1.0 * (self.within_vpg_errors / self.total_errors) * 100
        untagged_vlan = 1.0 * (self.untagged_vlan_errors / self.total_errors) * 100
        print("Percentage of errors that occur due to Across" +
                " Fabric VN/VLAN combinations is {0}%".format(across_percent))
        print("Percentage of errors that occur due to Duplicate Untagged Vlans is {0}%".
                format(untagged_vlan))
        print("Percentage of errors that occur due to invalid" +
                "VN/VLAN combinations within vpgs is {0}%".format(within_vpg))

        invalid_percent = (1.0 *self.invalid_vpgs / self._num_total_vpgs) * 100 
        print("Invalid VPG percentage is {0}".format(invalid_percent))
# End of class FabricVPGValidator

def _parse_args(args_str):
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawTextHelpFormatter,
        description='')

    parser.add_argument(
        '-v', '--version', action='version',
        version='%(prog)s ' + __version__
    )
    
    parser.add_argument(
        "--debug", help="Run in debug mode, default False",
        action='store_true', default=False
    )
    
    parser.add_argument(
        "--to-json", help="File to dump json to", default=None
    )

    ts = calendar.timegm(time.gmtime())
    if os.path.isdir("/var/log/contrail"):
        default_log = "/var/log/contrail/fabric_validation-{0}.log".format(ts)
    else:
        import tempfile
        default_log = '{0}/fabric_validation-{1}.log'.format(
                tempfile.gettempdir(), ts)

    parser.add_argument(
        "--log_file", help="Log file to save output, default '%(default)s'",
        default=default_log
    )

    args_obj, _ = parser.parse_known_args(args_str.split())
    _args = args_obj

    return _args
# end _parse_args

def main():
    args = _parse_args(' '.join(sys.argv[1:]))
    # Create Fabric Validator object
    fabric_validator = FabricVPGValidator(args)
    fabric_validator._validation_check_within_fabric()
    if fabric_validator._args.to_json != None:
        # check if the backup directory exists
        default_dir = fabric_validator._args.to_json
        with open(default_dir, 'w') as f:
            json.dump(fabric_validator.validation_failures, 
                    f, indent=3, sort_keys=True)
    else:
        fabric_validator._report_validation_error_in_fabric()
    fabric_validator._report_statistics()
if __name__ == "__main__":
    main()
