#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import VirtualPortGroup
from vnc_api.gen.resource_xsd import VpgInterfaceParametersType

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualPortGroupServer(ResourceMixin, VirtualPortGroup):

    @classmethod
    def _notify_ae_id_modified(cls, obj_dict=None, notify=False):
        if (obj_dict.get('deallocated_ae_id') and
                len(obj_dict.get('deallocated_ae_id'))):
            dealloc_dict_list = obj_dict.get('deallocated_ae_id')
            for dealloc_dict in dealloc_dict_list:
                ae_id = dealloc_dict.get('ae_id')
                vpg_name = dealloc_dict.get('vpg_name')
                prouter_name = dealloc_dict.get('prouter_name')
                cls.vnc_zk_client.free_ae_id(
                    prouter_name, ae_id,
                    vpg_name, notify=notify)
        if (obj_dict.get('allocated_ae_id') and
                len(obj_dict.get('allocated_ae_id'))):
            alloc_dict_list = obj_dict.get('allocated_ae_id')
            for alloc_dict in alloc_dict_list:
                ae_id = alloc_dict.get('ae_id')
                vpg_name = alloc_dict.get('vpg_name')
                prouter_name = alloc_dict.get('prouter_name')
                cls.vnc_zk_client.alloc_ae_id(prouter_name, vpg_name, ae_id,
                                              notify=True)

    @classmethod
    def _alloc_ae_id(cls, prouter_name, vpg_name):
        pi_ae = cls.vnc_zk_client.alloc_ae_id(prouter_name, vpg_name)
        attr_obj = VpgInterfaceParametersType(pi_ae)
        attr_dict = attr_obj.__dict__
        alloc_dict = {
            'ae_id': pi_ae,
            'prouter_name': prouter_name,
            'vpg_name': vpg_name,
        }
        return attr_dict, alloc_dict

    @classmethod
    def _dealloc_ae_id(cls, prouter_name, ae_id, vpg_name):
        cls.vnc_zk_client.free_ae_id(prouter_name, ae_id, vpg_name)
        dealloc_dict = {
            'ae_id': ae_id,
            'prouter_name': prouter_name,
            'vpg_name': vpg_name
        }
        return dealloc_dict

    @classmethod
    def _process_alloc_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
        attr_dict = None
        alloc_dealloc_dict = {'allocated_ae_id': [], 'deallocated_ae_id': []}
        alloc_list = []
        dealloc_list = []
        curr_pr_dict = {}
        curr_pi_dict = {}
        db_pi_dict = {}
        db_pr_dict = {}
        extra_deallocate_dict = {}
        vpg_uuid = db_obj_dict['uuid']
        if not obj_dict:
            obj_dict = {}

        # process incoming PIs
        for ref in obj_dict.get('physical_interface_refs') or []:
            curr_pi_dict[ref['uuid']] = ref['to'][1]
            curr_pr_dict[ref['to'][1]] = ref['attr']

        # process existing PIs in DB
        for ref in db_obj_dict.get('physical_interface_refs') or []:
            db_pi_dict[ref['uuid']] = ref['to'][1]
            if not (ref['to'][1] in db_pr_dict and db_pr_dict[ref['to'][1]]):
                db_pr_dict[ref['to'][1]] = ref['attr']

        create_pi_uuids = list(set(curr_pi_dict.keys()) - set(db_pi_dict.keys()))
        delete_pi_uuids = list(set(db_pi_dict.keys()) - set(curr_pi_dict.keys()))

        # no PIs in db_obj_dict
        if len(create_pi_uuids) < 2 and len(db_pi_dict.keys()) == 0:
            return True, (attr_dict, alloc_dealloc_dict)

        # nothing to delete or add
        if len(create_pi_uuids) == len(delete_pi_uuids) == 0:
            return True, (attr_dict, alloc_dealloc_dict)

        # nothing to delete, because rest of PIs shares same PR
        if (len(create_pi_uuids) == 0 and len(delete_pi_uuids) == 1 and
           len(db_pr_dict.keys()) == 1 and len(db_pi_dict.keys()) > 2):
            return True, (attr_dict, alloc_dealloc_dict)

        # allocate case
        for pi_uuid in create_pi_uuids:
            attr_dict = None
            pi_pr = curr_pi_dict.get(pi_uuid)
            pi_ae = db_pr_dict.get(pi_pr)
            if pi_ae is None:
                # allocate
                attr_dict, _alloc_dict = cls._alloc_ae_id(pi_pr, vpg_name)
                alloc_dealloc_dict['allocated_ae_id'].append(_alloc_dict)
                msg = "Allocating AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                    attr_dict, pi_uuid, vpg_name, pi_pr)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_INFO)
            else:
                attr_dict = pi_ae

            # re-allocate existing single PI if any
            if (len(db_pi_dict.keys()) == 1 and len(create_pi_uuids) == 1):
                db_pi_uuid = db_pi_dict.keys()[0]
                if (db_pi_dict.values()[0] != curr_pi_dict.get(create_pi_uuids[0])):
                    # allocate a new ae-id as it belongs to different PR
                    db_pr = db_pi_dict.values()[0]
                    attr_dict_leftover_pi, _alloc_dict = cls._alloc_ae_id(db_pr, vpg_name)
                    alloc_dealloc_dict['allocated_ae_id'].append(_alloc_dict)
                    msg = "Allocating AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                        attr_dict_leftover_pi, db_pi_uuid, vpg_name, db_pr)
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_INFO)
                else:
                    attr_dict_leftover_pi = attr_dict
                    msg = "Re-using AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                        attr_dict_leftover_pi, db_pi_uuid, vpg_name, pi_pr)
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_INFO)
                (ok, result) = cls.db_conn.ref_update(
                    'virtual_port_group',
                    vpg_uuid,
                    'physical_interface',
                    db_pi_uuid,
                    {'attr': attr_dict_leftover_pi},
                    'ADD',
                    db_obj_dict.get('id_perms'),
                    attr_to_publish=None,
                    relax_ref_for_delete=True)
                msg = "Updated AE-ID(%s) in PI(%s) ref to VPG(%s)" % (
                    attr_dict_leftover_pi, db_pi_uuid, vpg_name)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_INFO)

        # deallocate case
        _in_dealloc_list = []
        for pi_uuid in delete_pi_uuids:
            pi_pr = db_pi_dict.get(pi_uuid)
            pi_ae = db_pr_dict.get(pi_pr)
            db_pi_prs = db_pi_dict.values().count(pi_pr)
            # PR/VPG is already considered for deallocation, so no need
            # to dealloc again
            if '%s:%s' % (pi_pr, vpg_name) in _in_dealloc_list:
                continue
            if (pi_ae is not None and (db_pi_prs < 2 or
               len(delete_pi_uuids) > 1)):
                ae_id = pi_ae.get('ae_num')
                # de-allocate
                _dealloc_dict = cls._dealloc_ae_id(pi_pr, ae_id, vpg_name)
                alloc_dealloc_dict['deallocated_ae_id'].append(_dealloc_dict)
                # record deallocated pr/vpg
                _in_dealloc_list.append('%s:%s' % (pi_pr, vpg_name))
                msg = "Deallocated AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                    ae_id, pi_uuid, vpg_name, pi_pr)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_INFO)

        # de-allocate leftover single PI, if any
        # in delete case, whatever comes in curr_pi_dict are the
        # leftovers because for delete refs, ref to be deleted
        # will not be coming in payload
        if (len(curr_pi_dict.keys()) == 1 and
            len(db_pi_dict.keys()) == len(delete_pi_uuids) + 1):
            pi_uuid = curr_pi_dict.keys()[0]
            pi_pr = curr_pi_dict.get(pi_uuid)
            pi_ae = curr_pr_dict.get(pi_pr)
            if '%s:%s' % (pi_pr, vpg_name) not in _in_dealloc_list:
                if pi_ae is not None:
                    ae_id = pi_ae.get('ae_num')
                    _dealloc_dict = cls._dealloc_ae_id(pi_pr, ae_id, vpg_name)
                    alloc_dealloc_dict['deallocated_ae_id'].append(
                        _dealloc_dict)
                    # record deallocated pr/vpg
                    _in_dealloc_list.append('%s:%s' % (pi_pr, vpg_name))
                    msg = ("Deallocated AE-ID(%s) from leftover PI(%s) at "
                          "VPG(%s)/PR(%s)" % (
                        ae_id, pi_uuid, vpg_name, pi_pr))
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_INFO)
            pi_ae = db_pr_dict.get(pi_pr)
            (ok, result) = cls.db_conn.ref_update(
                'virtual_port_group',
                vpg_uuid,
                'physical_interface',
                pi_uuid,
                {'attr': None},
                'ADD',
                db_obj_dict.get('id_perms'),
                attr_to_publish=None,
                relax_ref_for_delete=True)

        return True, (attr_dict, alloc_dealloc_dict)

    @classmethod
    def update_physical_intf_type(cls, obj_dict=None,
                                  old_obj_dict=None):
        api_server = cls.server
        db_conn = cls.db_conn

        if not obj_dict:
            obj_dict = {}
        if not old_obj_dict:
            old_obj_dict = {}

        new_uuid_list = []
        old_uuid_list = []

        for phys_intf_ref in obj_dict.get('physical_interface_refs') or []:
            if phys_intf_ref.get('uuid') is None:
                try:
                    pi_uuid = db_conn.fq_name_to_uuid(
                        'physical_interface', phys_intf_ref.get('to'))
                except NoIdError as e:
                    return False, (400, str(e))
            else:
                pi_uuid = phys_intf_ref['uuid']

            new_uuid_list.append(pi_uuid)

        for phys_intf_ref in old_obj_dict.get(
                'physical_interface_refs') or []:
            if phys_intf_ref.get('uuid') is None:
                try:
                    pi_uuid = db_conn.fq_name_to_uuid(
                        'physical_interface', phys_intf_ref.get('to'))
                except NoIdError as e:
                    return False, (400, str(e))
            else:
                pi_uuid = phys_intf_ref['uuid']

            old_uuid_list.append(pi_uuid)

        to_be_added_pi_uuids = list(set(new_uuid_list) - set(old_uuid_list))
        to_be_deleted_pi_uuids = list(set(old_uuid_list) - set(new_uuid_list))

        # ensure this PI do not belong to other VPGs
        pis_attached_to_vpg = {}
        for pi_uuid in to_be_added_pi_uuids:
            ok, pi_obj_dict = db_conn.dbe_read(
                obj_type='physical-interface',
                obj_id=pi_uuid,
                obj_fields=['virtual_port_group_back_refs'])
            if not ok:
                return ok, (400, pi_obj_dict)
            vpg_refs = pi_obj_dict.get('virtual_port_group_back_refs')
            if vpg_refs:
                pis_attached_to_vpg[pi_uuid] = vpg_refs
        if pis_attached_to_vpg:
            vpg_uuid = obj_dict.get('uuid')
            msg = ""
            for pi, vpgs in pis_attached_to_vpg.items():
                for vpg in vpgs:
                   msg += (
                       'PI (%s) VPG-UUID (%s) VPG-FQNAME (%s); ' % (
                       pi, vpg['uuid'],  ":".join(vpg['to'])))
            return (
                False,
                (400, "physical interfaces already added at other VPGs can not"
                      " be attached to this VPG (%s): %s" % (vpg_uuid, msg)))

        for pi_uuid in to_be_added_pi_uuids or []:
            try:
                api_server.internal_request_update(
                    'physical_interface',
                    pi_uuid,
                    {'physical_interface_type': 'access'},
                )
            except HttpError as e:
                db_conn.config_log("PI (%s) add update failed (%s)" %
                                   (pi_uuid, str(e)),
                                   level=SandeshLevel.SYS_WARN)

        for pi_uuid in to_be_deleted_pi_uuids or []:
            try:
                api_server.internal_request_update(
                    'physical_interface',
                    pi_uuid,
                    {'physical_interface_type': None},
                )
            except HttpError as e:
                db_conn.config_log("PI (%s) delete update failed (%s)" %
                                   (pi_uuid, str(e)),
                                   level=SandeshLevel.SYS_WARN)

        return True, ''
    # end update_physical_intf_type

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ret_val = ''
        if ('vpg-internal' in obj_dict['fq_name'][2] and
                obj_dict.get('virtual_port_group_user_created', True)):
            msg = "Virtual port group(%s) with name vpg-internal as prefix "\
                  "can only be created internally"\
                  % (obj_dict['uuid'])
            return False, (400, msg)

        # when PI refs are added to VPG object during create VPG.
        # stateful_create do not allow us to allocate AE-ID and
        # update them in PI object refs
        if obj_dict.get('physical_interface_refs'):
            msg = ("API Infra do not support allocating AE-ID when "
                   "Physical Interface refs are sent in VPG create request. "
                   "Workaround: Create VPG first, then add Physical "
                   "Interface to VPG")
            return False, (400, msg)

        if obj_dict.get('virtual_port_group_trunk_port_id'):
            primary_vmi_id = obj_dict.get('virtual_port_group_trunk_port_id')
            ok, result = db_conn.dbe_read(
                obj_type='virtual_machine_interface',
                obj_id=primary_vmi_id,
                obj_fields=['virtual_port_group_back_refs'])

            if not ok:
                return (ok, 400, result)

            if result.get('virtual_port_group_back_refs'):
                msg = ("Trunk Port(%s) already belongs to following VPG(s): "
                       % (primary_vmi_id))
                for back_ref in result.get('virtual_port_group_back_refs'):
                    msg += ("\tUUID(%s), FQ_NAME(%s)" %
                            (back_ref['uuid'], ':'.join(back_ref['to'])))
                return (False, (409, msg))

        ok, result = cls.update_physical_intf_type(obj_dict)
        if not ok:
            return ok, result

        return True, ret_val

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # Handling both deletion and addition of interfaces here
        ret_val = ''

        if obj_dict.get('physical_interface_refs'):
            # compute the already existing physical interface refs for the
            # vpg object
            ok, db_obj_dict = db_conn.dbe_read(
                obj_type='virtual_port_group',
                obj_id=obj_dict['uuid'],
                obj_fields=['physical_interface_refs', 'id_perms'])
            if not ok:
                return ok, (400, db_obj_dict)

            ok, res = cls.update_physical_intf_type(obj_dict, db_obj_dict)
            if not ok:
                return ok, res
            ok, res = cls._process_alloc_ae_id(db_obj_dict, fq_name[-1], obj_dict)
            if not ok:
                return ok, res
            if res[0] and kwargs.get('ref_args'):
                kwargs['ref_args']['data']['attr'] = res[0]
                ret_val = res[1]

        return True, ret_val
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ret_val = ''
        # If the user deletes VPG, make sure that all the referring
        # VMIs are deleted.
        if obj_dict.get('virtual_machine_interface_refs'):
            msg = "Virtual port group(%s) can not be deleted as refernces "\
                  "to VMI and BMS instance association still exists."\
                  % (obj_dict['uuid'])
            return (False, (400, msg), None)

        ok, result = cls.update_physical_intf_type(old_obj_dict=obj_dict)
        if not ok:
            return (False, result, None)

        if obj_dict.get('physical_interface_refs'):
            # release ae-ids associated with PIs attached to this VPG
            fq_name = obj_dict.get('fq_name')
            ok, res = cls._process_alloc_ae_id(obj_dict, fq_name[-1])
            if not ok:
                return (ok, res, None)
            ret_val = res[1]

        return True, ret_val, None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        if obj_dict.get('virtual_port_group_user_created') is False:
            fq_name = obj_dict['fq_name']
            vpg_id = int(fq_name[2].split('-')[2])
            vpg_id_fqname = cls.vnc_zk_client.get_vpg_from_id(vpg_id)
            cls.vnc_zk_client.free_vpg_id(vpg_id, vpg_id_fqname)

        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        if obj_dict.get('virtual_port_group_user_created') is False:
            fq_name = obj_dict['fq_name']
            vpg_id = int(fq_name[2].split('-')[2])
            vpg_id_fqname = cls.vnc_zk_client.get_vpg_from_id(vpg_id)
            cls.vnc_zk_client.alloc_vpg_id(vpg_id_fqname, vpg_id)
        # Notify AE-ID allocation/de-allocation
        cls._notify_ae_id_modified(obj_dict)
        return True, ''

    @classmethod
    def dbe_update_notification(cls, obj_id, obj_dict=None):
        if obj_dict is not None:
            cls._notify_ae_id_modified(obj_dict, notify=True)
        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        fq_name = obj_dict['fq_name']
        if obj_dict.get('virtual_port_group_user_created') is False:
            vpg_id = int(fq_name[2].split('-')[2])
            vpg_id_fqname = cls.vnc_zk_client.get_vpg_from_id(vpg_id)
            cls.vnc_zk_client.free_vpg_id(vpg_id, vpg_id_fqname, notify=True)
        # Notify AE-ID allocation/de-allocation
        cls._notify_ae_id_modified(obj_dict)
        return True, ''
