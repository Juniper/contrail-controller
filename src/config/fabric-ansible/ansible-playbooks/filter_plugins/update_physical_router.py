#!/usr/bin/python

from builtins import str
from builtins import object
import json
import sys
import traceback

from filter_utils import _task_done, _task_error_log, _task_log, \
    FilterLog
from vnc_api.exceptions import NoIdError

from job_manager.job_utils import JobVncApi

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")


class FilterModule(object):
    """
    Filter used to consolidate all the physical router updates.

    For instance, we update at present the loopback ip, dataplane ip,
    os_version and annotations. Instead of having to read the prouter
    object every time, consolidating all the device update operation
    into a single filter functionality.
    """

    def filters(self):
        return {
            'update_physical_router': self.update_physical_router,
        }
    # end filters

    @staticmethod
    def _parse_additional_prop_and_upd_payload(
            obj_dict_payload, prouter_vendor, is_ztp):
        if obj_dict_payload.get("additional_properties"):
            annotations = {
                "key_value_pair": [
                    {
                        "key": prouter_vendor,
                        "value": json.dumps(
                            obj_dict_payload.get("additional_properties"))
                    }
                ]
            }
            obj_dict_payload["annotations"] = annotations
        pr_asn = obj_dict_payload.get("physical_router_asn")
        if pr_asn and not is_ztp:
            asn = {
                "asn": [int(pr_asn)]
            }
            obj_dict_payload["physical_router_autonomous_system"] = asn
        obj_dict_payload.pop("additional_properties")
        obj_dict_payload.pop("physical_router_asn")
    # end _parse_additional_prop_and_upd_payload

    def update_physical_router(self, job_ctx, prouter_name,
                               obj_dict_payload, prouter_vendor):
        """
        Updating the physical router object.

        :param job_ctx: Dictionary.
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
                    }]
                }
            }
        :param prouter_name: String
            example: "5c3-qfx8"
        :param obj_dict_payload: Dictionary
            example:
            {
              "physical_router_dataplane_ip": "10.0.0.2",
              "physical_router_loopback_ip": "10.0.0.2",
              "fq_name": ["default-global-system-config","5c3-qfx8"],
              "physical_router_os_version": "srx-20.65",
              "additional_properties": {
                  'Some Key that needs to be tapped':
                      'Some random annotations spec. to this vendor'
              }
            }
        :param prouter_vendor: String
            example: "juniper"

        :return: Dictionary
        if success, returns
            {
              'status': 'success',
              'upd_pr_log': <String: upd_pr_log>,
              'physical_router_upd_resp':
                      <Dictionary: physical_router_upd_resp>
            }
        if failure, returns
            {
              'status': 'failure',
              'error_msg': <String: exception message>,
              'upd_pr_log': <String: upd_pr_log>

            }
            :param physical_router_upd_resp: Dictionary
                example:
                {
                  "job_log_msg": <String: job log message>,
                  "warning_info": <Dictionary: warning_info>
                }
        """
        FilterLog.instance("UpdatePhysicalRouterFilter", prouter_name)
        _task_log("Starting Device Update")

        try:

            _task_log("Creating vnc handle")

            self.vnc_lib = JobVncApi.vnc_init(job_ctx)

            _task_log("Parsing additional physical router properties")

            is_ztp = job_ctx.get('job_input').get('manage_underlay')

            FilterModule._parse_additional_prop_and_upd_payload(
                obj_dict_payload, prouter_vendor, is_ztp)

            _task_log("Updating the physical router")

            physical_router_upd_resp = \
                self._update_physical_router_object(
                    obj_dict_payload,
                    prouter_name
                )
            _task_done()

            return {
                'status': 'success',
                'upd_pr_log': FilterLog.instance().dump(),
                'physical_router_upd_resp': physical_router_upd_resp
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'upd_pr_log': FilterLog.instance().dump()
                    }
    # end update_physical_router

    def _update_physical_router_object(self, obj_dict_payload, prouter_name):

        warning_info = {}

        object_type = "physical_router"

        try:
            cls = JobVncApi.get_vnc_cls(object_type)
            physical_router_obj = cls.from_dict(**obj_dict_payload)
            self.vnc_lib.physical_router_update(physical_router_obj)
            job_log_msg = "- loopback_ip: %s\n   - OS Version: %s\n" \
                          % (
                              physical_router_obj.
                              get_physical_router_loopback_ip(),
                              physical_router_obj.
                              get_physical_router_os_version()
                          )

        except NoIdError as exc:
            _task_error_log(str(exc))
            _task_error_log(traceback.format_exc())
            job_log_msg = "The device being updated was not found" \
                          " in the vnc database"
            warning_info = {
                "device_name": prouter_name,
                "obj_dict_payload": obj_dict_payload,
                "warning_message": str(exc)
            }

        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            job_log_msg = "There was a problem while updating the" \
                " device"
            warning_info = {
                "device_name": prouter_name,
                "obj_dict_payload": obj_dict_payload,
                "warning_message": str(ex)
            }

        return {
            "warning_info": warning_info,
            "job_log_msg": job_log_msg
        }

    # end _update_physical_router_object
