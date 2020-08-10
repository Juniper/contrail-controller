#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import os

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.exceptions import ResourceExistsError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import VirtualPortGroup
from vnc_api.gen.resource_xsd import VpgInterfaceParametersType

from vnc_cfg_api_server.context import get_context
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
                msg = "NOTIFY: Deallocated AE-ID (%s) at VPG(%s)/PR(%s)" % (
                    ae_id, vpg_name, prouter_name)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        if (obj_dict.get('allocated_ae_id') and
                len(obj_dict.get('allocated_ae_id'))):
            alloc_dict_list = obj_dict.get('allocated_ae_id')
            for alloc_dict in alloc_dict_list:
                ae_id = alloc_dict.get('ae_id')
                vpg_name = alloc_dict.get('vpg_name')
                prouter_name = alloc_dict.get('prouter_name')
                cls.vnc_zk_client.alloc_ae_id(prouter_name, vpg_name, ae_id,
                                              notify=True)
                msg = "NOTIFY: Allocated AE-ID (%s) at VPG(%s)/PR(%s)" % (
                    ae_id, vpg_name, prouter_name)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

    @classmethod
    def _alloc_ae_id(cls, prouter_name, vpg_name, vpg_uuid):
        # create vpg node at /id/ae-id-vpg/
        vpg_zk_path = os.path.join(
            cls.vnc_zk_client._vpg_ae_id_zk_path_prefix,
            'vpg:%s' % vpg_uuid,
            prouter_name)
        pi_ae = None
        if not cls.vnc_zk_client._zk_client.exists(vpg_zk_path):
            while True:
                try:
                    ae_id = cls.vnc_zk_client.alloc_ae_id(
                        prouter_name, vpg_name)

                    msg = "Reserving AE-ID(%s) for prouter(%s):vpg(%s) " % (
                        ae_id, prouter_name, vpg_name)
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

                    def undo_alloc_ae_id():
                        ok, result = cls._dealloc_ae_id(
                            prouter_name, ae_id, vpg_name, vpg_uuid)
                        return ok, result
                    get_context().push_undo(undo_alloc_ae_id)
                    break
                except ResourceExhaustionError:
                    # reraise if its real exhaustion
                    in_use_aes = 0
                    vpg_nodes = cls.vnc_zk_client._zk_client.get_children(
                        cls.vnc_zk_client._vpg_ae_id_zk_path_prefix)
                    for vpg_node in vpg_nodes:
                        pr_path = cls.vnc_zk_client._zk_client.exists(
                            os.path.join(
                                cls.vnc_zk_client._vpg_ae_id_zk_path_prefix,
                                vpg_node, prouter_name))
                        if pr_path:
                            in_use_aes += 1
                    if in_use_aes >= cls.vnc_zk_client._AE_MAX_ID:
                        err_msg = ('ResourceExhaustionError: when allocating '
                                   'AE-ID for virtual-port-group (%s) at '
                                   'physical-router (%s)' % (
                                       vpg_name, prouter_name))
                        return False, (400, err_msg)
                    msg = "Retry reserving AE-ID for prouter(%s)/vpg(%s) " % (
                        prouter_name, vpg_name)
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            try:
                cls.vnc_zk_client._zk_client.create_node(
                    vpg_zk_path, ae_id)
                msg = "Create ZK-node(%s) with AE-ID(%s) is successful" % (
                    vpg_zk_path, ae_id)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

                def undo_create_node():
                    cls.vnc_zk_client._zk_client.delete_node(
                        vpg_zk_path, True)
                get_context().push_undo(undo_create_node)
                pi_ae = ae_id
            except ResourceExistsError:
                ok, result = cls._dealloc_ae_id(
                    prouter_name, ae_id, vpg_name, vpg_uuid)
                if not ok:
                    return ok, result
                pi_ae_str = cls.vnc_zk_client._zk_client.read_node(vpg_zk_path)
                pi_ae = int(pi_ae_str)
                msg = ("Reusing AE-ID (%s) from ZK-node(%s) after "
                       "ResourceExistsError" % (pi_ae, vpg_zk_path))
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                # TO-DO: can read_node return empty?
        else:
            # TO-DO: can read_node return empty?
            pi_ae_str = cls.vnc_zk_client._zk_client.read_node(vpg_zk_path)
            pi_ae = int(pi_ae_str)
            msg = "Reusing AE-ID (%s) from  ZK-node(%s)" % (
                pi_ae, vpg_zk_path)
            cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        # TO-DO: Can pi_ae remain None at any case?
        # if pi_ae is None:
        attr_obj = VpgInterfaceParametersType(pi_ae)
        attr_dict = attr_obj.__dict__
        alloc_dict = {
            'ae_id': pi_ae,
            'prouter_name': prouter_name,
            'vpg_name': vpg_name,
        }
        msg = "Allocated AE-ID (%s) at VPG(%s)/PR(%s)" % (
            pi_ae, vpg_name, prouter_name)
        cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        return True, (attr_dict, alloc_dict)

    @classmethod
    def _dealloc_ae_id(cls, prouter_name, ae_id, vpg_name, vpg_uuid):
        # delete znode
        vpg_zk_path = os.path.join(
            cls.vnc_zk_client._vpg_ae_id_zk_path_prefix,
            'vpg:%s' % vpg_uuid,
            prouter_name)
        pi_ae_str = cls.vnc_zk_client._zk_client.read_node(vpg_zk_path)
        pi_ae = int(pi_ae_str) if pi_ae_str else None
        # check if given ae_id is same as one found in zknode path
        if pi_ae is not None and pi_ae == ae_id:
            ae_id = pi_ae
            cls.vnc_zk_client._zk_client.delete_node(vpg_zk_path)
            msg = "Delete ZK-node(%s) is successful" % vpg_zk_path
            cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        cls.vnc_zk_client.free_ae_id(prouter_name, ae_id, vpg_name)
        msg = "De-allocated AE-ID (%s) at VPG(%s)/PR(%s)" % (
            ae_id, vpg_name, prouter_name)
        cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        return True, ''

    @classmethod
    def _process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
        attr_dict = None
        alloc_dealloc_dict = {'allocated_ae_id': [], 'deallocated_ae_id': []}
        curr_pr_dict = {}
        curr_pi_dict = {}
        db_pi_dict = {}
        db_pr_dict = {}
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

        create_pi_uuids = list(set(curr_pi_dict.keys()) -
                               set(db_pi_dict.keys()))
        delete_pi_uuids = list(set(db_pi_dict.keys()) -
                               set(curr_pi_dict.keys()))

        # no PIs in db_obj_dict
        if len(create_pi_uuids) < 2 and len(db_pi_dict.keys()) == 0:
            msg = "Skip AE-ID allocation as Creating PI len(%s) < 2" % (
                create_pi_uuids)
            cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            return True, (attr_dict, alloc_dealloc_dict)

        # nothing to delete or add
        if len(create_pi_uuids) == len(delete_pi_uuids) == 0:
            msg = "Skip AE-ID allocation as no PI to Create / Delete"
            cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            return True, (attr_dict, alloc_dealloc_dict)

        # nothing to delete, because rest of PIs shares same PR
        if (len(create_pi_uuids) == 0 and len(delete_pi_uuids) == 1 and
           len(db_pr_dict.keys()) == 1 and len(db_pi_dict.keys()) > 2):
            msg = "Skip AE-ID allocation as rest PI(%s) shares same PR(%s)" % (
                db_pi_dict.keys(), db_pr_dict.keys())
            cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            return True, (attr_dict, alloc_dealloc_dict)

        # allocate case
        for pi_uuid in create_pi_uuids:
            attr_dict = None
            pi_pr = curr_pi_dict.get(pi_uuid)
            pi_ae = db_pr_dict.get(pi_pr)
            if pi_ae is None:
                # allocate
                ok, result = cls._alloc_ae_id(pi_pr, vpg_name, vpg_uuid)
                if not ok:
                    return ok, result
                attr_dict, _alloc_dict = result
                alloc_dealloc_dict['allocated_ae_id'].append(_alloc_dict)
                msg = "Allocated AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                    attr_dict, pi_uuid, vpg_name, pi_pr)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            else:
                attr_dict = pi_ae

            # re-allocate existing single PI if any
            if (len(db_pi_dict.keys()) == 1 and len(create_pi_uuids) == 1):
                db_pi_uuid = list(db_pi_dict.keys())[0]
                if (list(db_pi_dict.values())[0] !=
                        curr_pi_dict.get(create_pi_uuids[0])):
                    # allocate a new ae-id as it belongs to different PR
                    db_pr = list(db_pi_dict.values())[0]
                    ok, result = cls._alloc_ae_id(db_pr, vpg_name, vpg_uuid)
                    if not ok:
                        return ok, result
                    attr_dict_leftover_pi, _alloc_dict = result
                    alloc_dealloc_dict['allocated_ae_id'].append(_alloc_dict)

                    def undo_append_alloc_dict():
                        try:
                            alloc_dealloc_dict['allocated_ae_id'].remove(
                                _alloc_dict)
                        except ValueError:
                            pass
                    get_context().push_undo(undo_append_alloc_dict)
                    msg = ("Allocated AE-ID(%s) for PI(%s) at "
                           "VPG(%s)/PR(%s)" % (
                               attr_dict_leftover_pi, db_pi_uuid,
                               vpg_name, db_pr))
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                    attr_to_publish = alloc_dealloc_dict
                else:
                    attr_dict_leftover_pi = attr_dict
                    msg = "Re-using AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                        attr_dict_leftover_pi, db_pi_uuid, vpg_name, pi_pr)
                    cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                    attr_to_publish = None
                (ok, result) = cls.db_conn.ref_update(
                    'virtual_port_group',
                    vpg_uuid,
                    'physical_interface',
                    db_pi_uuid,
                    {'attr': attr_dict_leftover_pi},
                    'ADD',
                    db_obj_dict.get('id_perms'),
                    attr_to_publish=attr_to_publish,
                    relax_ref_for_delete=True)
                if not ok:
                    return ok, result

                def undo_ref_update():
                    return cls.db_conn.ref_update(
                        'virtual_port_group',
                        vpg_uuid,
                        'physical_interface',
                        db_pi_uuid,
                        {'attr': None},
                        'ADD',
                        db_obj_dict.get('id_perms'),
                        attr_to_publish=attr_to_publish,
                        relax_ref_for_delete=True)
                get_context().push_undo(undo_ref_update)
                msg = "Updated AE-ID(%s) in PI(%s) ref to VPG(%s)" % (
                    attr_dict_leftover_pi, db_pi_uuid, vpg_name)
                cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        # deallocate case
        _in_dealloc_list = []
        for pi_uuid in delete_pi_uuids:
            pi_pr = db_pi_dict.get(pi_uuid)
            pi_ae = db_pr_dict.get(pi_pr)
            db_pi_prs = list(db_pi_dict.values()).count(pi_pr)
            # PR/VPG is already considered for deallocation, so no need
            # to dealloc again
            if '%s:%s' % (pi_pr, vpg_name) in _in_dealloc_list:
                continue
            if (pi_ae is not None and (db_pi_prs < 2 or
               len(delete_pi_uuids) > 1)):
                ae_id = pi_ae.get('ae_num')
                # de-allocation moved to post_dbe_update
                _dealloc_dict = {
                    'ae_id': ae_id,
                    'prouter_name': pi_pr,
                    'vpg_uuid': vpg_uuid,
                    'vpg_name': vpg_name}
                alloc_dealloc_dict['deallocated_ae_id'].append(_dealloc_dict)

                def undo_add_dealloc_dict():
                    alloc_dealloc_dict['deallocated_ae_id'].remove(
                        _dealloc_dict)
                get_context().push_undo(undo_add_dealloc_dict)
                # record deallocated pr/vpg
                _in_dealloc_list.append('%s:%s' % (pi_pr, vpg_name))

        # de-allocate leftover single PI, if any
        # in delete case, whatever comes in curr_pi_dict are the
        # leftovers because for delete refs, ref to be deleted
        # will not be coming in payload
        if (len(curr_pi_dict.keys()) == 1 and
                len(db_pi_dict.keys()) == len(delete_pi_uuids) + 1):
            pi_uuid = list(curr_pi_dict.keys())[0]
            pi_pr = curr_pi_dict.get(pi_uuid)
            pi_ae = curr_pr_dict.get(pi_pr)
            if '%s:%s' % (pi_pr, vpg_name) not in _in_dealloc_list:
                if pi_ae is not None:
                    ae_id = pi_ae.get('ae_num')
                    # de-allocation moved to post_dbe_update
                    _dealloc_dict = {
                        'ae_id': ae_id,
                        'prouter_name': pi_pr,
                        'vpg_uuid': vpg_uuid,
                        'vpg_name': vpg_name}
                    alloc_dealloc_dict['deallocated_ae_id'].append(
                        _dealloc_dict)

                    def undo_add_dealloc_dict():
                        alloc_dealloc_dict['deallocated_ae_id'].remove(
                            _dealloc_dict)
                    get_context().push_undo(undo_add_dealloc_dict)
                    # record deallocated pr/vpg
                    _in_dealloc_list.append('%s:%s' % (pi_pr, vpg_name))
            # TO-DO Add undo pi-ref for leftover pi
            # remove PI ref from VPG
            (ok, result) = cls.db_conn.ref_update(
                'virtual_port_group',
                vpg_uuid,
                'physical_interface',
                pi_uuid,
                {'attr': None},
                'ADD',
                db_obj_dict.get('id_perms'),
                relax_ref_for_delete=True)
            if not ok:
                return ok, result

            def undo_ae_dealloc_from_pi():
                attr_obj = VpgInterfaceParametersType(_dealloc_dict['ae_id'])
                (ok, result) = cls.db_conn.ref_update(
                    'virtual_port_group',
                    vpg_uuid,
                    'physical_interface',
                    pi_uuid,
                    {'attr': attr_obj},
                    'ADD',
                    db_obj_dict.get('id_perms'),
                    relax_ref_for_delete=True)
            get_context().push_undo(undo_ae_dealloc_from_pi)
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
                            pi, vpg['uuid'], ":".join(vpg['to'])))
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
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn, **kwargs):
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
            # Allocate/Deallocate AE-IDs for the attached PIs
            ok, res = cls._process_ae_id(
                db_obj_dict, fq_name[-1], obj_dict)
            if not ok:
                return ok, res
            if res[0] and kwargs.get('ref_update'):
                kwargs['ref_update']['data']['attr'] = res[0]
            ret_val = res[1]
        return True, ret_val
    # end pre_dbe_update

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ret_val = ''
        # Handling de-allocation of AE-IDs from ZK
        if not obj_dict:
            return True, ret_val
        vpg_uuid = obj_dict.get('uuid')
        vpg_name = fq_name[-1]
        ref_update_result = kwargs.get('ref_update') or {}
        deallocated_list = ref_update_result.get('deallocated_ae_id') or []
        for deallocate in deallocated_list:
            ok, result = cls._dealloc_ae_id(
                deallocate['prouter_name'],
                deallocate['ae_id'],
                deallocate['vpg_name'],
                deallocate['vpg_uuid'])
            if not ok:
                return ok, result
        ok, db_obj_dict = db_conn.dbe_read(
            obj_type='virtual_port_group',
            obj_id=vpg_uuid,
            obj_fields=['physical_interface_refs', 'id_perms'])
        if not ok:
            return ok, db_obj_dict
        alloc_dealloc_dict = {'allocated_ae_id': [], 'deallocated_ae_id': []}
        pi_refs = db_obj_dict.get('physical_interface_refs') or []
        if len(pi_refs) < 2:
            return True, ''
        for pi_ref in pi_refs:
            prouter_name = pi_ref['to'][1]
            vpg_zk_path = os.path.join(
                cls.vnc_zk_client._vpg_ae_id_zk_path_prefix,
                'vpg:%s' % vpg_uuid,
                prouter_name)
            ae_pi = None
            if pi_ref.get('attr') is not None:
                ae_at_pi = pi_ref.get('attr') or {}
                ae_pi = ae_at_pi.get('ae_num')
            pi_uuid = pi_ref['uuid']
            if ae_pi is not None:
                ae_id_str = cls.vnc_zk_client._zk_client.read_node(
                    vpg_zk_path)
                ae_id = int(ae_id_str)
                # allocated ae-id still remains at ZK
                if ae_pi == ae_id:
                    continue
            ok, result = cls._alloc_ae_id(prouter_name, vpg_name, vpg_uuid)
            if not ok:
                return ok, result
            attr_dict, _alloc_dict = result
            alloc_dealloc_dict['allocated_ae_id'].append(_alloc_dict)
            msg = "Allocated AE-ID(%s) for PI(%s) at VPG(%s)/PR(%s)" % (
                attr_dict, pi_uuid, vpg_name, prouter_name)
            cls.db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            if attr_dict.get('ae_num') is not None:
                (ok, result) = cls.db_conn.ref_update(
                    'virtual_port_group',
                    vpg_uuid,
                    'physical_interface',
                    pi_uuid,
                    {'attr': attr_dict},
                    'ADD',
                    db_obj_dict.get('id_perms'),
                    attr_to_publish=alloc_dealloc_dict,
                    relax_ref_for_delete=True)
                if not ok:
                    return ok, result
        return True, ret_val

    # end post_dbe_update
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
            ok, res = cls._process_ae_id(obj_dict, fq_name[-1])
            if not ok:
                return (ok, res, None)
            ret_val = res[1]

        return True, ret_val, None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        # remove AE-IDs from PI ref allocated to this VPG
        pi_refs = obj_dict.get('physical_interface_refs') or []
        vpg_uuid = obj_dict.get('uuid')
        for pi_ref in pi_refs:
            vpg_name = obj_dict.get('fq_name')[-1]
            pi_ae = None
            pi_ae_attr = pi_ref.get('attr') or {}
            if pi_ae_attr is not None:
                pi_ae = pi_ae_attr.get('ae_num')
            if pi_ae is not None:
                prouter_name = pi_ref['to'][1]
                ok, result = cls._dealloc_ae_id(
                    prouter_name, pi_ae, vpg_name, vpg_uuid)
                if not ok:
                    return ok, result

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
