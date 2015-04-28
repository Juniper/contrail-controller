#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import os
import gevent
import logging
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent
import kazoo.recipe.election
from kazoo.client import KazooState
from kazoo.retry import KazooRetry

from bitarray import bitarray
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError
from gevent.coros import BoundedSemaphore

import uuid

LOG_DIR = '/var/log/contrail/'

class IndexAllocator(object):

    def __init__(self, zookeeper_client, path, size=0, start_idx=0, 
                 reverse=False,alloc_list=None, max_alloc=0):
        if alloc_list is None:
            self._alloc_list = [{'start':start_idx, 'end':start_idx+size}]
        else:
            sorted_alloc_list = sorted(alloc_list, key=lambda k: k['start'])
            self._alloc_list = sorted_alloc_list

        alloc_count = len(self._alloc_list)
        total_size = 0
        start_idx = self._alloc_list[0]['start']
        size = 0

        #check for overlap in alloc_list --TODO
        for alloc_idx in range (0, alloc_count -1):
            idx_start_addr = self._alloc_list[alloc_idx]['start']
            idx_end_addr = self._alloc_list[alloc_idx]['end']
            next_start_addr = self._alloc_list[alloc_idx+1]['start']
            if next_start_addr <= idx_end_addr:
                raise Exception()
            size += idx_end_addr - idx_start_addr + 1
        size += self._alloc_list[alloc_count-1]['end'] - self._alloc_list[alloc_count-1]['start'] + 1

        self._size = size
        self._start_idx = start_idx
        if max_alloc == 0:
            self._max_alloc = self._size
        else:
            self._max_alloc = max_alloc

        self._zookeeper_client = zookeeper_client
        self._path = path
        self._in_use = bitarray('0')
        self._reverse = reverse
        for idx in self._zookeeper_client.get_children(path):
            idx_int = self._get_bit_from_zk_index(int(idx))
            if idx_int >= 0:
                self._set_in_use(idx_int)
        # end for idx
    # end __init__

    def _get_zk_index_from_bit(self, idx):
        size = idx
        if self._reverse:
            for alloc in reversed(self._alloc_list):
                size -= alloc['end'] - alloc['start'] + 1
                if size < 0:
                   return alloc['start']-size - 1
        else:
            for alloc in self._alloc_list:
                size -= alloc['end'] - alloc['start'] + 1
                if size < 0:
                   return alloc['end']+size + 1

        raise Exception()
    # end _get_zk_index

    def _get_bit_from_zk_index(self, idx):
        size = 0
        if self._reverse:
            for alloc in reversed(self._alloc_list):
                if alloc['start'] <= idx <= alloc['end']:
                    return alloc['end'] - idx + size
                size += alloc['end'] - alloc['start'] + 1
            pass
        else:
            for alloc in self._alloc_list:
                if alloc['start'] <= idx <= alloc['end']:
                    return idx - alloc['start'] + size
                size += alloc['end'] - alloc['start'] + 1
        return -1
    # end _get_bit_from_zk_index

    def _set_in_use(self, bitnum):
        # if the index is higher than _max_alloc, do not use the bitarray, in
        # order to reduce the size of the bitarray. Otherwise, set the bit
        # corresponding to idx to 1 and extend the _in_use bitarray if needed
        if bitnum > self._max_alloc:
            return
        if bitnum >= self._in_use.length():
            temp = bitarray(bitnum - self._in_use.length())
            temp.setall(0)
            temp.append('1')
            self._in_use.extend(temp)
        else:
            self._in_use[bitnum] = 1
    # end _set_in_use

    def _reset_in_use(self, bitnum):
        # if the index is higher than _max_alloc, do not use the bitarray, in
        # order to reduce the size of the bitarray. Otherwise, set the bit
        # corresponding to idx to 1 and extend the _in_use bitarray if needed
        if bitnum > self._max_alloc:
            return
        if bitnum >= self._in_use.length():
            return
        else:
            self._in_use[bitnum] = 0
    # end _reset_in_use

    def set_in_use(self, idx):
        bit_idx = self._get_bit_from_zk_index(idx)
        if bit_idx < 0:
            return
        self._set_in_use(bit_idx)
    # end set_in_use

    def reset_in_use(self, idx):
        bit_idx = self._get_bit_from_zk_index(idx)
        if bit_idx < 0:
            return
        self._reset_in_use(bit_idx)
    # end reset_in_use

    def alloc(self, value=None):
        if self._in_use.all():
            idx = self._in_use.length()
            if idx > self._max_alloc:
                raise ResourceExhaustionError()
            self._in_use.append(1)
        else:
            idx = self._in_use.index(0)
            self._in_use[idx] = 1

        idx = self._get_zk_index_from_bit(idx)
        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._zookeeper_client.create_node(self._path + id_str, value)
            return idx
        except ResourceExistsError:
            return self.alloc(value)
    # end alloc

    def reserve(self, idx, value=None):
        bit_idx = self._get_bit_from_zk_index(idx)
        if bit_idx < 0:
            return None  
        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._zookeeper_client.create_node(self._path + id_str, value)
            self._set_in_use(bit_idx)
            return idx
        except ResourceExistsError:
            self._set_in_use(bit_idx)
            existing_value = self.read(idx)
            if (value == existing_value or
                existing_value is None): # upgrade case
                # idempotent reserve
                return idx
            msg = 'For index %s reserve conflicts with existing value %s.' \
                  %(idx, existing_value)
            self._zookeeper_client.syslog(msg, level='notice')
            return None
    # end reserve

    def delete(self, idx):
        id_str = "%(#)010d" % {'#': idx}
        self._zookeeper_client.delete_node(self._path + id_str)
        bit_idx = self._get_bit_from_zk_index(idx)
        if 0 <= bit_idx < self._in_use.length():
            self._in_use[bit_idx] = 0
    # end delete

    def read(self, idx):
        id_str = "%(#)010d" % {'#': idx}
        id_val = self._zookeeper_client.read_node(self._path+id_str)
        if id_val is not None:
            bit_idx = self._get_bit_from_zk_index(idx)
            if bit_idx >= 0:
                self._set_in_use(bit_idx)
        return id_val
    # end read

    def empty(self):
        return not self._in_use.any()
    # end empty

    @classmethod
    def delete_all(cls, zookeeper_client, path):
        try:
            zookeeper_client.delete_node(path, recursive=True)
        except kazoo.exceptions.NotEmptyError:
            #TODO: Add retries for NotEmptyError
            zookeeper_client.syslog("NotEmptyError while deleting %s" % path)
    # end delete_all

#end class IndexAllocator


class ZookeeperClient(object):

    def __init__(self, module, server_list, logging_fn=None):
        # logging
        logger = logging.getLogger(module)
        logger.setLevel(logging.INFO)
        try:
            handler = logging.handlers.RotatingFileHandler(LOG_DIR + module + '-zk.log', maxBytes=10*1024*1024, backupCount=5)
        except IOError:
            print "Cannot open log file in %s" %(LOG_DIR)
        else:
            log_format = logging.Formatter('%(asctime)s [%(name)s]: %(message)s', datefmt='%m/%d/%Y %I:%M:%S %p')
            handler.setFormatter(log_format)
            logger.addHandler(handler)

        if logging_fn:
            self.log = logging_fn
        else:
            self.log = self.syslog

        self._zk_client = \
            kazoo.client.KazooClient(
                server_list,
                timeout=400,
                handler=kazoo.handlers.gevent.SequentialGeventHandler(),
                logger=logger)

        self._zk_client.add_listener(self._zk_listener)
        self._logger = logger
        self._election = None
        self._server_list = server_list
        # KazooRetry to retry keeper CRUD operations
        self._retry = KazooRetry(max_tries=None, sleep_func=gevent.sleep)

        self._conn_state = None
        self._sandesh_connection_info_update(status='INIT', message='')

        self.connect()

    # end __init__

    # start
    def connect(self):
        while True:
            try:
                self._zk_client.start()
                break
            except gevent.event.Timeout as e:
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                gevent.sleep(1)
            # Zookeeper is also throwing exception due to delay in master election
            except Exception as e:
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                gevent.sleep(1)
        # Update connection info
        self._sandesh_connection_info_update(status='UP', message='')

    # end

    def is_connected(self):
        return self._zk_client.state == KazooState.CONNECTED
    # end is_connected

    def syslog(self, msg, *args, **kwargs):
        if not self._logger:
            return
        level = kwargs.get('level', 'info')
        log_method = getattr(self._logger, level, self._logger.info)
        log_method(msg)
    # end syslog

    def _zk_listener(self, state):
        if state == KazooState.CONNECTED:
            if self._election:
                self._election.cancel()
            # Update connection info
            self._sandesh_connection_info_update(status='UP', message='')
        elif state == KazooState.LOST:
            # Lost the session with ZooKeeper Server
            # Best of option we have is to exit the process and restart all 
            # over again
            os._exit(2)
        elif state == KazooState.SUSPENDED:
            # Update connection info
            self._sandesh_connection_info_update(status='INIT',
                message = 'Connection to zookeeper lost. Retrying')

    # end

    def _zk_election_callback(self, func, *args, **kwargs):
        func(*args, **kwargs)
        # Exit if running master encounters error or exception
        exit(1)
    # end

    def master_election(self, path, identifier, func, *args, **kwargs):
        while True:
            self._election = self._zk_client.Election(path, identifier)
            self._election.run(self._zk_election_callback, func, *args, **kwargs)
    # end master_election

    def create_node(self, path, value=None):
        try:
            if value is None:
                value = uuid.uuid4()
            retry = self._retry.copy()
            retry(self._zk_client.create, path, str(value), makepath=True)
        except kazoo.exceptions.NodeExistsError:
            current_value = self.read_node(path)
            if current_value == value:
                return True;
            raise ResourceExistsError(path, str(current_value))
    # end create_node

    def delete_node(self, path, recursive=False):
        try:
            retry = self._retry.copy()
            retry(self._zk_client.delete, path, recursive=recursive)
        except kazoo.exceptions.NoNodeError:
            pass
        except Exception as e:
            raise e
    # end delete_node

    def read_node(self, path):
        try:
            retry = self._retry.copy()
            value = retry(self._zk_client.get, path)
            return value[0]
        except Exception:
            return None
    # end read_node

    def get_children(self, path):
        try:
            retry = self._retry.copy()
            return retry(self._zk_client.get_children, path)
        except Exception:
            return []
    # end read_node

    def _sandesh_connection_info_update(self, status, message):
        from pysandesh.connection_info import ConnectionState
        from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
            ConnectionType
        from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

        new_conn_state = getattr(ConnectionStatus, status)
        ConnectionState.update(conn_type = ConnectionType.ZOOKEEPER,
                name = 'Zookeeper', status = new_conn_state,
                message = message,
                server_addrs = self._server_list.split(','))

        if (self._conn_state and self._conn_state != ConnectionStatus.DOWN and
            new_conn_state == ConnectionStatus.DOWN):
            msg = 'Connection to Zookeeper down: %s' %(message)
            self.log(msg, level=SandeshLevel.SYS_ERR)
        if (self._conn_state and self._conn_state != new_conn_state and
            new_conn_state == ConnectionStatus.UP):
            msg = 'Connection to Zookeeper ESTABLISHED'
            self.log(msg, level=SandeshLevel.SYS_NOTICE)

        self._conn_state = new_conn_state
    # end _sandesh_connection_info_update

# end class ZookeeperClient
