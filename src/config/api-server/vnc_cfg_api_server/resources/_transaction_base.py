#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import time
import uuid

from cfgm_common import jsonutils as json
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class TransactionResourceBase(ResourceMixin):
    @classmethod
    def _pr_name_to_id(cls, db_conn, pr_name):
        pr_fq_name = ['default-global-system-config', pr_name]
        try:
            return db_conn.fq_name_to_uuid('physical_router', pr_fq_name)
        except NoIdError:
            return None

    @classmethod
    def _pi_id_to_fqname(cls, db_conn, pi_uuid):
        try:
            return db_conn.uuid_to_fq_name(pi_uuid)
        except NoIdError:
            return None

    @classmethod
    def _upd_job_transaction(cls, trans_info,
                             pr_name_list=None, pr_id_list=None,
                             pr_refs=None,
                             pi_fqname_list=None, pi_id_list=None,
                             pi_refs=None):

        pr_id_set = set()
        pr_name_set = set()

        if pr_name_list:
            pr_name_set.update(pr_name_list)

        if pr_id_list:
            pr_id_set.update(pr_id_list)

        # Collect all physical-interface fq_names
        for pi_fqname in pi_fqname_list or []:
            pr_name_set.add(pi_fqname[1])

        # Collect all physical-interface UUIDs, convert to
        # physical-router names and add to pr_name list
        for pi_id in list(set(pi_id_list or [])):
            pi_fqname = cls._pi_id_to_fqname(cls.db_conn, pi_id)
            if pi_fqname:
                pr_name_set.add(pi_fqname[1])

        # Collect all physical-interface references, convert to
        # physical-router names and add to pr_name list
        for pi_ref in pi_refs or []:
            pi_fqname = pi_ref.get('to')
            if pi_fqname is None:
                pi_id = pi_ref['uuid']
                pi_fqname = cls._pi_id_to_fqname(cls.db_conn, pi_id)
            if pi_fqname:
                pr_name_set.add(pi_fqname[1])

        # Collect all physical-router references, add to list of
        # physical-router UUIDs
        for pr_ref in pr_refs or []:
            pr_id = pr_ref.get('uuid')
            if pr_id is None:
                pr_id = cls._pr_name_to_id(cls.db_conn, pr_ref['to'][-1])
            pr_id_set.add(pr_id)

        # Now convert all physical-router names to UUIDs and add to main
        # list of physical-routers UUIDs
        for pr_name in list(pr_name_set):
            pr_id = cls._pr_name_to_id(cls.db_conn, pr_name)
            if pr_id:
                pr_id_set.add(pr_id)

        # If empty list, just exit
        if len(list(pr_id_set)) == 0:
            return True, ''

        # Using the UUID list, get all physical-router objects and write
        # the transaction information into the annotations field
        ok, pr_dict_list, _ = cls.db_conn.dbe_list(
            'physical_router', obj_uuids=list(pr_id_set),
            field_names=['annotations'])
        if ok:
            for pr_dict in pr_dict_list:

                trans_entry = {
                    'key': 'job_transaction',
                    'value': json.dumps(trans_info),
                }
                prop_collection_updates = [
                    {
                        'field': 'annotations',
                        'operation': 'set',
                        'value': trans_entry,
                        'position': 'job_transaction'
                    }
                ]
                try:
                    cls.server.internal_request_prop_collection(
                        pr_dict['uuid'], prop_collection_updates
                    )
                except HttpError as e:
                    cls.db_conn.config_log(
                        'PR Object {} transaction {}: {}: {} '.format(
                            uuid, json.dumps(trans_info),
                            e.status_code, e.content),
                        level=SandeshLevel.SYS_DEBUG)
                    return False, (e.status_code, e.content)

        return True, ''

    @classmethod
    def create_job_transaction(cls, transaction_descr, transaction_id='',
                               pr_name_list=None, pr_id_list=None,
                               pr_refs=None,
                               pi_fqname_list=None, pi_id_list=None,
                               pi_refs=None):

        if not transaction_id:
            transaction_id = str(int(round(time.time() * 1000))) + '_' + str(
                uuid.uuid4())
        trans_info = {'transaction_id': transaction_id,
                      'transaction_descr': transaction_descr}

        return cls._upd_job_transaction(trans_info,
                                        pr_name_list, pr_id_list, pr_refs,
                                        pi_fqname_list, pi_id_list,
                                        pi_refs)
