from builtins import object
import json
import logging

from vnc_api.gen.resource_xsd import (
    KeyValuePair,
    KeyValuePairs,
)

class FilterLog(object):
    """Pulbic class for filter logging."""

    _instance = None

    @staticmethod
    def instance(loggername=None, device_name=None):
        """Filter log instance."""
        if not FilterLog._instance:
            FilterLog._instance = FilterLog(loggername, device_name)
        return FilterLog._instance
    # end instance

    @staticmethod
    def cleanup_filterlog_instance():
        """Instance cleanup."""
        if FilterLog._instance:
            FilterLog._instance = None
    # end cleanup_filterlog_instance

    @staticmethod
    def _init_logging(loggername):
        """return: type=<logging.Logger>."""
        logger = logging.getLogger(loggername)
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)
        return logger
    # end _init_logging

    def __init__(self, loggername, device_name):
        """Init routine for filter logging."""
        self._msg = None
        self._logs = []
        if device_name is not None:
            loggername = loggername + " : " + device_name
        self._logger = FilterLog._init_logging(loggername)
    # end __init__

    def logger(self):
        """."""
        return self._logger
    # end logger

    def msg_append(self, msg):
        """."""
        if msg:
            if not self._msg:
                self._msg = msg + ' ... '
            else:
                self._msg += msg + ' ... '
    # end log

    def msg_end(self):
        """."""
        if self._msg:
            self._msg += 'done'
            self._logs.append(self._msg)
            self._logger.info(self._msg)
            self._msg = None
    # end msg_end

    def msg_info(self, msg):
        """."""
        self._logger.info(msg)
    # end msg_error

    def msg_error(self, msg):
        """."""
        self._logger.error(msg)
    # end msg_error

    def msg_debug(self, msg):
        """."""
        self._logger.debug(msg)
    # end msg_debug

    def msg_warn(self, msg):
        """."""
        self._logger.warn(msg)
    # end msg_warn

    def dump(self):
        """."""
        retval = ""
        for msg in self._logs:
            retval += msg + '\n'
        return retval
    # end dump
# end FilterLog


def _task_log(msg):
    FilterLog.instance().msg_append(msg)
# end _task_log


def _task_done(msg=None):
    if msg:
        _task_log(msg)
    FilterLog.instance().msg_end()
# end _task_done


def _task_info_log(msg):
    FilterLog.instance().msg_info(msg)
# end _task_info_log


def _task_error_log(msg):
    FilterLog.instance().msg_error(msg)
# end _task_error_log


def _task_debug_log(msg):
    FilterLog.instance().msg_debug(msg)
# end _task_debug_log


def _task_warn_log(msg):
    FilterLog.instance().msg_warn(msg)
# end _task_warn_log


# Get list of VNC objects given a object UUID list or parent UUID list
def vnc_bulk_get(vnc_api, obj_name, obj_uuids=None, parent_uuids=None,
                 fields=None):
    """Get bulk VNC object."""
    # search using object uuid list or parent uuid list
    chunk_size = 20
    obj_list = []
    chunk_idx = 0

    if obj_uuids and parent_uuids or obj_uuids and not parent_uuids:
        search_by_obj = True
        uuid_list = obj_uuids
        num_uuids = len(obj_uuids)
    elif parent_uuids:
        search_by_obj = False
        uuid_list = parent_uuids
        num_uuids = len(parent_uuids)
    else:
        return []

    while chunk_idx < num_uuids:
        chunk_uuids = uuid_list[chunk_idx:chunk_idx + chunk_size]
        chunk_obj_list = getattr(vnc_api, obj_name + "_list")(
            obj_uuids=chunk_uuids if search_by_obj else None,
            parent_id=chunk_uuids if not search_by_obj else None,
            fields=fields).get(obj_name.replace('_', '-'))
        obj_list += chunk_obj_list
        chunk_idx += chunk_size
    return obj_list
# end vnc_bulk_get

def get_job_transaction(job_ctx):
    transaction_id = job_ctx.get('job_transaction_id')
    transaction_descr = job_ctx.get('job_transaction_descr')
    return {'transaction_id': transaction_id,
            'transaction_descr': transaction_descr}

def set_job_transaction(device_obj, vnc_api, trans_info):
    trans_val = json.dumps(trans_info)
    annotations = device_obj.get_annotations()
    if not annotations:
        annotations = KeyValuePairs()
    annotations.add_key_value_pair(
        KeyValuePair(key='job_transaction', value=trans_val))
    device_obj.set_annotations(annotations)
    if vnc_api:
        vnc_api.physical_router_update(device_obj)


def set_fabric_job_transaction(job_ctx, vnc_api, trans_info):
    try:
        fabric_info = job_ctx.get('job_input')
        fabric_fq_name = fabric_info.get('fabric_fq_name')
        fabric_obj = vnc_api.fabric_read(fq_name=fabric_fq_name)
        trans_val = json.dumps(trans_info)
        annotations = fabric_obj.get_annotations()
        if not annotations:
            annotations = KeyValuePairs()
        annotations.add_key_value_pair(
            KeyValuePair(key='job_transaction', value=trans_val))
        fabric_obj.set_annotations(annotations)
        vnc_api.fabric_update(fabric_obj)
    except Exception:
        pass