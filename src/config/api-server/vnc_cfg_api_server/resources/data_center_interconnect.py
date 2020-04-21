#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import DataCenterInterconnect

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class DataCenterInterconnectServer(ResourceMixin, DataCenterInterconnect):

    @staticmethod
    def _is_this_src_lr(lr_ref):
        return True if lr_ref.get('attr') is not None else False

    @classmethod
    def _validate_dci_lrs_fabrics_based_on_dcitype(cls, db_conn, dci):
        """Validate for intra_fabric type dci properties.

           - at least one destination LR must present
           - at least one valid entry exist in VN_refs/or RP_Refs exist.
           - at least one PR must have selected per destination LR
           - LR ref count must be destination LR + source LR
           - ALL destination LR's and their PR list must exist.
           - If Src LR is master LR then
                - every selected DST PR must exist in master SRC PR AND
                - DO NOT ALLOW any DST Public LR
           - if src LR is Public LR then DST LR must not be master LR
           - if master LR is in DST LR then master LR selected DST PR must
           exist in Src LR PR
        """
        dci_type = dci.get('data_center_interconnect_type')
        if dci_type is None:
            dci_type = 'inter_fabric'
        if dci_type != 'intra_fabric':
            return cls._make_sure_lrs_belongs_to_different_fabrics(
                db_conn, dci)
        # for all intra_fabric type dci do following validation
        # get all lr uuid from lr_refs
        dst_lrs_uuid = []
        src_lr_uuid = None
        for lr_ref in dci.get('logical_router_refs') or []:
            lruuid = lr_ref.get('uuid')
            if cls._is_this_src_lr(lr_ref):
                if src_lr_uuid is not None:
                    return False, (
                        400, "More than one Source LR not allowed for "
                             "intra_fabric type data_center_interconnect.")
                src_lr_uuid = lruuid
            else:
                if lruuid in dst_lrs_uuid:
                    return False, (
                        400, "%s logical router entry exists more than one "
                             "in logical_router_refs." % lruuid)
                dst_lrs_uuid.append(lruuid)

        if src_lr_uuid is None:
            return False, (
                400, "No Source LR specified in "
                     "logical_router_refs for intra_fabric type "
                     "data_center_interconnect.")
        if len(dst_lrs_uuid) < 1:
            return False, (
                400, "No Destination LR specified in "
                     "logical_router_refs for intra_fabric type "
                     "data_center_interconnect.")

        # get all dst lr and their pr list into dictionary
        lr_to_pr = {}
        dci_dpr_list = dci.get('destination_physical_router_list')
        dci_lr_list = []
        if dci_dpr_list:
            dci_lr_list = dci_dpr_list.get('logical_router_list') or []
        dst_lr_uuid_set = set(dst_lrs_uuid)
        for dstlr in dci_lr_list:
            uuid = dstlr.get('logical_router_uuid') or None
            if not uuid:
                return False, (
                    400, "Invalid logical_router_uuid in "
                         "destination_physical_router_list.")
            if uuid == src_lr_uuid:
                return False, (
                    400, "%s logical_router_uuid exist in "
                         "destination_physical_router_list is marked as a "
                         " source LR." % uuid)
            if uuid not in dst_lr_uuid_set:
                return False, (
                    400, "%s logical_router_uuid in "
                         "destination_physical_router_list does not exist in "
                         "logical_router_refs" % uuid)
            dci_prlist = dstlr.get('physical_router_uuid_list') or []
            if len(dci_prlist) < 1:
                return False, (
                    400, "%s logical_router_uuid in "
                         "destination_physical_router_list does not have "
                         " any physical_router" % uuid)
            if uuid in lr_to_pr:
                return False, (
                    400, "%s logical_router_uuid in "
                         "destination_physical_router_list specified more "
                         "than once." % uuid)
            lr_to_pr[uuid] = dci_prlist
        if len(lr_to_pr) < 1:
            return False, (
                400, "No destination LR exist in "
                     "destination_physical_router_list")
        if len(lr_to_pr) != len(dst_lrs_uuid):
            return False, (
                400, "destination LR list count %s in "
                     "destination_physical_router_list does not match "
                     "logical_router_Refs expected %s count" %
                (len(lr_to_pr), len(dst_lrs_uuid)))

        # Validate routing policies if supplied.
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
        # validate src lr and related properties
        ok, lrsrc = cls.dbe_read(
            db_conn, 'logical_router', src_lr_uuid,
            obj_fields=['physical_router_refs',
                        'logical_router_gateway_external',
                        'display_name'])
        if not ok:
            return False, lrsrc
        srclr_public = lrsrc.get('logical_router_gateway_external', False)
        srclr_master = True if lrsrc.get('display_name', '') == 'master-LR'\
            else False
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
            dstlr_master = True if dstlr.get('display_name', '') == \
                'master-LR' else False
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
