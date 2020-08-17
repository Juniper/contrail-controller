#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import DataCenterInterconnect

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class DataCenterInterconnectServer(ResourceMixin, DataCenterInterconnect):

    @staticmethod
    def _is_this_src_lr(lr_ref):
        """Is lr_ref is of type source LR for intra_fabric type dci.

        : Args:
        : lr_ref: logical_router_ref element
        : return:
        : return True if lr_ref has attr defined as this lr_Ref entry
        : represents source LR type else return False
        """
        return True if lr_ref.get('attr') is not None else False

    @classmethod
    def _validate_dci_source_lr(cls, dci):
        """Validate and retrieve dci source lr and destination lrs.

        logical_router_refs must have single LR specified as source LR.
        At least one destination lr must exist in logical_router_refs
        : Args:
        : dci: data_Center_interconnect object of type intra_fabric
        : return:
        : return error message, source lr uuid, and destination lr uuid list.
        : On success, error message is empty.
        """
        dst_lrs_uuid = []
        src_lr_uuid = None
        msg = ""
        for lr_ref in dci.get('logical_router_refs') or []:
            lruuid = lr_ref.get('uuid')
            if cls._is_this_src_lr(lr_ref):
                if src_lr_uuid is not None:
                    msg = "More than one Source LR not allowed for " \
                          "intra_fabric type data_center_interconnect."
                    return msg, src_lr_uuid, dst_lrs_uuid
                src_lr_uuid = lruuid
            else:
                if lruuid in dst_lrs_uuid:
                    msg = "%s logical router entry exists more than one " \
                          "in logical_router_refs." % lruuid
                    return msg, src_lr_uuid, dst_lrs_uuid
                dst_lrs_uuid.append(lruuid)

        if src_lr_uuid is None:
            msg = "No Source LR specified in logical_router_refs for " \
                  "intra_fabric type data_center_interconnect."
        elif len(dst_lrs_uuid) < 1:
            msg = "No Destination LR specified in logical_router_refs for " \
                  "intra_fabric type data_center_interconnect."
        return msg, src_lr_uuid, dst_lrs_uuid

    @classmethod
    def _validate_dci_destination_lrs(cls, dci, src_lr_uuid, dst_lrs_uuid):
        """Validate and retrieve dci destination lr and their prs.

        Validate destination_physical_router_list property has proper valid
        destination LR and their PR list provided, it should not have
        source LR in this list.
        : Args:
        : dci: data_Center_interconnect object of type intra_fabric
        : src_lr_uuid: source lr uuid from dci lr_refs
        : dst_lrs_uuid: list of destination lr uuids from dci lr_refs
        : return:
        : return error message, and dictionary of destination lr to pr list.
        : On success, error message is empty.
        """
        lr_to_pr = {}
        dci_dpr_list = dci.get('destination_physical_router_list')
        dci_lr_list = []
        if dci_dpr_list:
            dci_lr_list = dci_dpr_list.get('logical_router_list') or []
        dst_lr_uuid_set = set(dst_lrs_uuid)
        for dstlr in dci_lr_list:
            uuid = dstlr.get('logical_router_uuid') or None
            if not uuid:
                return "Invalid logical_router_uuid in " \
                       "destination_physical_router_list.", lr_to_pr
            if uuid == src_lr_uuid:
                return "%s logical_router_uuid exist in " \
                       "destination_physical_router_list is marked as a " \
                       " source LR." % uuid, lr_to_pr
            if uuid not in dst_lr_uuid_set:
                return "%s logical_router_uuid in " \
                       "destination_physical_router_list does not exist in " \
                       "logical_router_refs" % uuid, lr_to_pr
            dci_prlist = dstlr.get('physical_router_uuid_list') or []
            if len(dci_prlist) < 1:
                return "%s logical_router_uuid in " \
                       "destination_physical_router_list does not have " \
                       " any physical_router" % uuid, lr_to_pr
            if uuid in lr_to_pr:
                return "%s logical_router_uuid in " \
                       "destination_physical_router_list specified more " \
                       "than once." % uuid, lr_to_pr
            lr_to_pr[uuid] = dci_prlist
        if len(lr_to_pr) < 1:
            return "No destination LR exist in " \
                   "destination_physical_router_list", lr_to_pr
        if len(lr_to_pr) != len(dst_lrs_uuid):
            return "destination LR list count %s in " \
                   "destination_physical_router_list does not match " \
                   "logical_router_Refs expected %s count" % \
                   (len(lr_to_pr), len(dst_lrs_uuid)), lr_to_pr
        return "", lr_to_pr

    @classmethod
    def _validate_dci_rp_and_vn(cls, dci):
        """Validate routing policies or virtual network refs.

        Either virtual_network_refs or routing_policy_refs must have
        provided. If virtual_network_refs provided, then it validates
        all this virtual_networks exist in the source LR
        : Args:
        : dci: data_Center_interconnect object of type intra_fabric
        : src_lr_uuid: source lr uuid from dci lr_refs
        : dst_lrs_uuid: list of destination lr uuids from dci lr_refs
        : return:
        : return error message, and dictionary of destination lr to pr list.
        : On success, error message is empty.
        """
        dci_rps = dci.get('routing_policy_refs') or []
        dci_vns = dci.get('virtual_network_refs') or []
        if len(dci_rps) > 0 and len(dci_vns) > 0:
            return False, (
                400, "Provide either routing_policy_refs or "
                     "virtual_network_refs. Both has been provided for "
                     "intra_fabric type data_center_interconnect")
        if len(dci_rps) == 0 and len(dci_vns) == 0:
            return False, (
                400, "Provide either routing_policy_refs or "
                     "virtual_network_refs. None of them provided for "
                     "intra_fabric type data_center_interconnect")
        return True, ""

    @classmethod
    def _validate_dci_lrs_fabrics_based_on_dcitype(cls, db_conn, dci):
        """Validate data_center_interconnect object based on its type.

        For inter_fabric dci object, it validates following condition by
        calling _make_sure_lrs_belongs_to_different_fabrics():
        - LR is not allowed to be use in more than one inter_fabric type
        dci object.
        - LR's all PR devices must exist in the same fabric.

        For intra_fabric dci object, it validates following condition:
        - All logical_router_uuid entries under logical_router_list
        must be valid and respective LR must exist in DB.
        - for every logical_router_uuid, it validates that this LR
        exist in DB and also specified in logical_router_refs as a
        of type non-source LR. Also it validates it has all PR devices
        specified in its property physical_router_uuid_list.
        - If Source LR name is "master-LR" then it validates
        following condition:
        i) every destination LR's physical_router_uuid_list device
        must also exist in master-LR's PR list.
        ii) None of destination LR must have property
        logical_router_gateway_external marked as a True.
        - If source LR's property logical_router_gateway_external is
        marked as True then any of destination LR name must not be
        the "master-LR"
        - If "master-LR" named LR exist in one of the destination LR List
        then master-LR's physical_router_uuid_list devices must also
        exist in source LR's physical_router device list.
        : Args:
        : db_conn: db connection handler
        : dci: data_center_interconnect object
        : return:
        : return True on success else returns False with proper
        : httpStatusCode and http Resonse error Message
        :
        """
        dci_type = dci.get('data_center_interconnect_type')
        if dci_type is None:
            dci_type = 'inter_fabric'
        if dci_type != 'intra_fabric':
            return cls._make_sure_lrs_belongs_to_different_fabrics(
                db_conn, dci)

        # for all intra_fabric type dci do following validation
        # validate dci lr_refs and get all lr uuid from lr_refs
        msg, src_lr_uuid, dst_lrs_uuid = cls._validate_dci_source_lr(dci)
        if len(msg) > 0:
            return False, (400, msg)

        # get all dst lr and their pr list into dictionary
        msg, lr_to_pr = cls._validate_dci_destination_lrs(dci, src_lr_uuid,
                                                          dst_lrs_uuid)
        if len(msg) > 0:
            return False, (400, msg)

        # Validate routing policies or virtual_network refs.
        ok, result = cls._validate_dci_rp_and_vn(dci)
        if not ok:
            return False, result

        # Do not allow to use same source LR in more than one intra_fabric
        # DCI object
        ok, lrsrc = cls.dbe_read(
            db_conn, 'logical_router', src_lr_uuid,
            obj_fields=['physical_router_refs',
                        'logical_router_gateway_external',
                        'data_center_interconnect_back_refs',
                        'display_name'])
        if not ok:
            return False, lrsrc

        for dciref in lrsrc.get('data_center_interconnect_back_refs') or []:
            if dciref.get('uuid') == dci.get('uuid'):
                continue
            if cls._is_this_src_lr(dciref):
                return False, (
                    400, "Source Logical router %s already associated with "
                         "DCI %s, Same source LR can not associate with more"
                         " than one intra_fabric type DCI." %
                    (lrsrc.get('display_name', src_lr_uuid),
                     dciref.get('to')[-1]))

        srclr_public = lrsrc.get('logical_router_gateway_external', False)
        srclr_master = True if 'master-LR' in (lrsrc.get('display_name') or
                                               '') else False
        srclr_prs = []
        for pr_ref in lrsrc.get('physical_router_refs') or []:
            pr_uuid = pr_ref.get('uuid')
            if pr_uuid:
                srclr_prs.append(pr_uuid)

        # validate all lr uuids in DB and their pr against dst lr pr list
        for lr_uuid, prlist in lr_to_pr.items():
            ok, dstlr = cls.dbe_read(
                db_conn, 'logical_router', lr_uuid,
                obj_fields=['physical_router_refs',
                            'logical_router_gateway_external',
                            'display_name'])
            if not ok:
                return False, dstlr
            dstlr_public = dstlr.get('logical_router_gateway_external', False)
            dstlr_master = True if 'master-LR' in (dstlr.get(
                'display_name') or '') else False
            if srclr_master is True and dstlr_public is True:
                return False, (
                    400, "master-LR as source logical router, destination LR "
                         "%s must not have logical_router_gateway_external"
                         " set to be True." % dstlr.get('display_name', ''))
            if srclr_public is True and dstlr_master is True:
                return False, (
                    400, "source logical router %s has "
                         "logical_router_gateway_external True, Destination "
                         "LR %s must not be a master-LR" %
                    (lrsrc.get('display_name', src_lr_uuid),
                     dstlr.get('display_name', lr_uuid)))
            dstlr_pr_list = []
            for pr_ref in dstlr.get('physical_router_refs') or []:
                pr_uuid = pr_ref.get('uuid')
                if pr_uuid:
                    dstlr_pr_list.append(pr_uuid)
            dstlr_pr_list_set = set(dstlr_pr_list)
            for pr in prlist:
                if pr not in dstlr_pr_list_set:
                    return False, (
                        400, "physical_router uuid %s does not exist in "
                             "destination LR %s" %
                        (pr, dstlr.get('display_name', '')))
                if srclr_master is True and pr not in srclr_prs:
                    return False, (
                        400, "master-LR as source logical router, destination"
                             " LR %s selected physical router uuid %s must "
                             "exists in master-LR's physical routers list." %
                        (dstlr.get('display_name', lr_uuid), pr))
                if dstlr_master is True and pr not in srclr_prs:
                    return False, (
                        400, "destination logical router as a master-LR, its "
                             "selected physical router uuid %s must exist in "
                             "source LR %s physical router list." %
                        (pr, lrsrc.get('display_name', src_lr_uuid)))
        return True, ''

    @classmethod
    def _make_sure_lrs_belongs_to_different_fabrics(cls, db_conn, dci):
        lr_list = []
        for lr_ref in dci.get('logical_router_refs') or []:
            lr_uuid = lr_ref.get('uuid')
            if lr_uuid:
                lr_list.append(lr_uuid)
        if not lr_list:
            return True, ''

        for lr_uuid in lr_list:
            ok, read_result = cls.dbe_read(
                db_conn, 'logical_router', lr_uuid, obj_fields=[
                    'physical_router_refs',
                    'data_center_interconnect_back_refs'])
            if not ok:
                return False, read_result
            lr = read_result
            # use only dci back ref with dci marked as interfabric
            lr_dcireflist = []
            for dciref in lr.get('data_center_interconnect_back_refs') or []:
                ok, dci = cls.dbe_read(
                    db_conn, 'data_center_interconnect', dciref.get('uuid'),
                    obj_fields=['data_center_interconnect_type'])
                if ok and dci:
                    dcitype = dci.get('data_center_interconnect_type')
                    if not dcitype or dcitype == 'inter_fabric':
                        lr_dcireflist.append(dciref)
            # check there are no more than one DCI back ref for this LR
            # it is acceptable that dci object can associate with a lr,
            # but lr not associated with any PRs yet
            # make sure LR update should check for this association
            if lr_dcireflist and len(lr_dcireflist) > 1:
                msg = ("Logical router can not associate with more than one "
                       "inter_fabric type DCI: %s" % lr_uuid)
                return False, (400, msg)
            if lr.get('lr_dcireflist') and len(lr.lr_dcireflist) == 1:
                dci_ref = lr_dcireflist[0]
                if dci.get('fq_name') != dci_ref.get('to'):
                    msg = ("Logical router can not associate with more than "
                           "one inter_fabric type DCI: %s" % lr_uuid)
                    return False, (400, msg)
            init_fab = None
            for pr_ref in read_result.get('physical_router_refs') or []:
                pr_uuid = pr_ref.get('uuid')
                status, pr_result = cls.dbe_read(
                    db_conn, 'physical_router', pr_uuid, obj_fields=[
                        'fabric_refs'])
                if not status:
                    return False, pr_result

                if pr_result.get("fabric_refs"):
                    # pr can be associated to only one Fabric, if not, many
                    # other system components will fail fabric implementation
                    # must ensure this, no need to double check
                    fab_id = pr_result["fabric_refs"][0].get('uuid')
                    if init_fab and init_fab != fab_id:
                        msg = ("Logical router can not associate with PRs "
                               "belonging to different DCI connected Fabrics: "
                               "%s" % lr_uuid)
                        return False, (400, msg)
                    else:
                        init_fab = fab_id
            if not init_fab:
                msg = ("DCI Logical router is not associated to any fabric: %s"
                       % lr_uuid)
                return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # make sure referenced LRs belongs to different fabrics
        return cls._validate_dci_lrs_fabrics_based_on_dcitype(
            db_conn, obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(
            db_conn, 'data_center_interconnect', id,
            obj_fields=['logical_router_refs',
                        'data_center_interconnect_type'
                        ])
        if not ok:
            return ok, read_result
        # changes to DCI type not allowed
        old_dci_type = read_result.get('data_center_interconnect_type')
        new_dci_type = obj_dict.get('data_center_interconnect_type')
        intrafabric = False
        if old_dci_type and old_dci_type == 'intra_fabric':
            intrafabric = True
        if old_dci_type and new_dci_type:
            if old_dci_type != new_dci_type:
                return False, (
                    403, "Cannot change data_center_interconnect_type. Please"
                         " specify data_center_interconnect_type as '%s'" %
                    old_dci_type)
        # for intra_fabric, changes to source LR (first LR) not allowed
        if intrafabric is True and obj_dict.get('logical_router_refs'):
            def _get_source_lr(lr_refs, check_single_lr):
                source_lr = None
                for lr in lr_refs or []:
                    if cls._is_this_src_lr(lr):
                        if check_single_lr is False:
                            source_lr = lr.get('uuid')
                            return True, source_lr
                        if source_lr is None:
                            source_lr = lr.get('uuid')
                        elif source_lr != lr.get('uuid'):
                            return False, source_lr
                if source_lr is None:
                    return False, source_lr
                return True, source_lr
            ok, new_src_lr = _get_source_lr(
                obj_dict.get('logical_router_refs'), True)
            if new_src_lr is None:
                return False, (
                    403, "No Source LR specified in "
                         "logical_router_refs for intra_fabric type "
                         "data_center_interconnect.")
            if not ok:
                return False, (
                    403, "More than one Source LR not allowed for "
                         "intra_fabric type data_center_interconnect.")
            _, old_src_lr = _get_source_lr(
                read_result.get('logical_router_refs'), False)
            if old_src_lr is not None and new_src_lr != old_src_lr:
                return False, (
                    403, "Cannot change Source LR for intra_fabric type"
                         "data_center_interconnect.")
        # make sure referenced LRs belongs to fabrics based on dcitype
        return cls._validate_dci_lrs_fabrics_based_on_dcitype(
            db_conn, obj_dict)
