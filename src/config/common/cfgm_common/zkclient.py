from __future__ import print_function
from __future__ import unicode_literals
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from builtins import zip
from builtins import str
from builtins import range
from builtins import object
from future.utils import native_str
import os
import gevent
import logging
import kazoo.client
import kazoo.exceptions
import kazoo.handlers.gevent
import kazoo.recipe.election
from kazoo.client import KazooState
from kazoo.retry import KazooRetry, ForceRetryError
from kazoo.recipe.counter import Counter

from bitarray import bitarray
from cfgm_common.exceptions import ResourceExhaustionError,\
     ResourceExistsError, OverQuota
from gevent.lock import BoundedSemaphore

import datetime
import uuid
import sys
import socket

LOG_DIR = '/var/log/contrail/'

class IndexAllocator(object):

    def __init__(self, zookeeper_client, path, size=0, start_idx=0,
                 reverse=False, alloc_list=None, max_alloc=0):
        self._size = size
        self._start_idx = start_idx
        if alloc_list is None:
            self._alloc_list = [{'start': start_idx, 'end': start_idx+size}]
        else:
            sorted_alloc_list = sorted(alloc_list, key=lambda k: k['start'])
            self._alloc_list = sorted_alloc_list

        size = self._get_range_size(self._alloc_list)

        if max_alloc == 0:
            self._max_alloc = size
        else:
            self._max_alloc = max_alloc

        self._zookeeper_client = zookeeper_client
        self._path = path
        self._in_use = bitarray(native_str('0'))
        self._reverse = reverse
        for idx in self._zookeeper_client.get_children(path):
            idx_int = self._get_bit_from_zk_index(int(idx))
            if idx_int >= 0:
                self._set_in_use(self._in_use, idx_int)
        # end for idx
    # end __init__

    # Given a set of ranges (alloc_list), return
    # the cumulative count of the ranges.
    def _get_range_size(self, alloc_list):
        alloc_count = len(alloc_list)
        size = 0

        # check for overlap in alloc_list --TODO
        for alloc_idx in range(0, alloc_count - 1):
            idx_start_addr = alloc_list[alloc_idx]['start']
            idx_end_addr = alloc_list[alloc_idx]['end']
            next_start_addr = alloc_list[alloc_idx+1]['start']
            if next_start_addr <= idx_end_addr:
                raise Exception(
                    'Allocation Lists Overlapping: %s' % (alloc_list))
            size += idx_end_addr - idx_start_addr + 1
        size += (alloc_list[alloc_count-1]['end'] -
                 alloc_list[alloc_count-1]['start'] + 1)

        return size

    def _has_ranges_shrunk(self, old_list, new_list):
        if len(old_list) > len(new_list):
            return True

        for old_pool, new_pool in zip(old_list, new_list):
            if (new_pool['start'] > old_pool['start'] or
                new_pool['end'] < old_pool['end']):
                return True

        return False

    # Reallocates the indexes to a new set of indexes provided by
    # the user.
    # Limitation -
    # 1. No. of alloc pools needs to be constant
    #    For example, [10-20] cannot become [10-20],[25-30]
    # 2. Every alloc pool can only expand but not shrink
    #    For ex, [10-20] can become [9-20] or [10-22] or [9-22]
    #    but not [15-17]
    #
    def reallocate(self, new_alloc_list):
        sorted_alloc_list = sorted(new_alloc_list,
                                   key=lambda k: k['start'])

        if self._has_ranges_shrunk(self._alloc_list, sorted_alloc_list):
            raise Exception('Indexes allocated cannot be shrunk: %s' %
                            (self._alloc_list))

        size = self._get_range_size(sorted_alloc_list)
        self._max_alloc = size

        new_in_use = bitarray(0)
        for idx, bitval in enumerate(self._in_use):
            if not bitval:
                continue
            zk_idx = self._get_zk_index_from_bit(idx, sorted_alloc_list)
            idx_int = self._get_bit_from_zk_index(zk_idx, sorted_alloc_list)

            if idx_int >= 0:
                self._set_in_use(new_in_use, idx_int)

        self._in_use = new_in_use
        self._alloc_list = sorted_alloc_list
        # end for idx

    def _get_zk_index_from_bit(self, idx, alloc_list=None):
        if not alloc_list:
            alloc_list = self._alloc_list
        size = idx
        if self._reverse:
            for alloc in reversed(alloc_list):
                size -= alloc['end'] - alloc['start'] + 1
                if size < 0:
                    return alloc['start'] - size - 1
        else:
            for alloc in alloc_list:
                size -= alloc['end'] - alloc['start'] + 1
                if size < 0:
                    return alloc['end']+size + 1

        raise ResourceExhaustionError(
            'Cannot get zk index from bit %s' % (idx))
    # end _get_zk_index

    def _get_bit_from_zk_index(self, idx, alloc_list=None):
        if not alloc_list:
            alloc_list = self._alloc_list
        size = 0
        if self._reverse:
            for alloc in reversed(alloc_list):
                if alloc['start'] <= idx <= alloc['end']:
                    return alloc['end'] - idx + size
                size += alloc['end'] - alloc['start'] + 1
            pass
        else:
            for alloc in alloc_list:
                if alloc['start'] <= idx <= alloc['end']:
                    return idx - alloc['start'] + size
                size += alloc['end'] - alloc['start'] + 1
        return -1
    # end _get_bit_from_zk_index

    def _set_in_use(self, array, bitnum):
        # if the index is higher than _max_alloc, do not use the bitarray, in
        # order to reduce the size of the bitarray. Otherwise, set the bit
        # corresponding to idx to 1 and extend the _in_use bitarray if needed
        if bitnum > self._max_alloc:
            return
        if bitnum >= array.length():
            temp = bitarray(bitnum - array.length())
            temp.setall(0)
            temp.append('1')
            array.extend(temp)
        else:
            array[bitnum] = 1
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
        self._set_in_use(self._in_use, bit_idx)
    # end set_in_use

    def reset_in_use(self, idx):
        bit_idx = self._get_bit_from_zk_index(idx)
        if bit_idx < 0:
            return
        self._reset_in_use(bit_idx)
    # end reset_in_use

    def get_alloc_count(self):
        return self._in_use.count()
    # end get_alloc_count

    def _alloc_from_pools(self, pools=None):
        if not pools:
            raise ResourceExhaustionError()

        if self._reverse:
            pools = list(reversed(pools))
        for pool in pools:
            last_idx = self._in_use.length() - 1
            pool_start = pool['start']
            pool_end = pool['end']
            pool_size = pool_end - pool_start + 1
            if self._reverse:
                start_zk_idx = pool_end
                end_zk_idx = pool_start
            else:
                start_zk_idx = pool_start
                end_zk_idx = pool_end
            start_bit_idx = self._get_bit_from_zk_index(start_zk_idx)
            end_bit_idx = self._get_bit_from_zk_index(end_zk_idx)

            # if bitarray is less then start_bit_index,
            # extend bit array to start_bit_idx and use that idx
            if last_idx < start_bit_idx:
                temp = bitarray(start_bit_idx - last_idx)
                temp.setall(0)
                self._in_use.extend(temp)
                self._in_use[start_bit_idx] = 1
                return start_bit_idx

            # if bitarray is in between start_bit_idx and end_bit_idx
            if last_idx >= start_bit_idx and last_idx <= end_bit_idx:
                # we need to slice part of bitarray from
                # start of the pool and end of array
                pool_bitarray = self._in_use[start_bit_idx:]
            else:
                pool_bitarray = self._in_use[
                    start_bit_idx:end_bit_idx+1]
            if pool_bitarray.all():
                if last_idx >= end_bit_idx:
                    continue
                idx = self._in_use.length()
                self._in_use.append(1)
            else:
                idx = pool_bitarray.index(0)
                idx += start_bit_idx

            self._in_use[idx] = 1
            return idx

        raise ResourceExhaustionError()
    # end _alloc_from_pools

    def alloc(self, value=None, pools=None):
        if pools:
            idx = self._alloc_from_pools(pools)
        else:
            # Allocates a index from the allocation list
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
            return self.alloc(value, pools)
    # end alloc

    def reserve(self, idx, value=None, pools=None):
        # Reserves the requested index if available
        if not self._start_idx <= idx < self._start_idx + self._size:
            return None

        if pools:
            for pool in pools:
                if pool['start'] <= idx <= pool['end']:
                    break
            else:
                return

        try:
            # Create a node at path and return its integer value
            id_str = "%(#)010d" % {'#': idx}
            self._zookeeper_client.create_node(self._path + id_str, value)
            self.set_in_use(idx)
            return idx
        except ResourceExistsError:
            self.set_in_use(idx)
            existing_value = self.read(idx)
            if (value == existing_value):
                # idempotent reserve
                return idx
            msg = 'For index %s reserve conflicts with existing value %s.' \
                  % (idx, existing_value)
            self._zookeeper_client.syslog(msg, level='notice')
            raise
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
        id_val = self._zookeeper_client.read_node(self._path + id_str)
        if id_val is not None:
            bit_idx = self._get_bit_from_zk_index(idx)
            if bit_idx >= 0:
                self._set_in_use(self._in_use, bit_idx)
        return id_val
    # end read

    def empty(self):
        return not self._in_use.any()
    # end empty

    def get_last_allocated_id(self):
        if self.empty():
            return

        if self._reverse:
            return self._get_zk_index_from_bit(self._in_use.index(1))

        # TODO: since 1.2.0, bitarray have rindex in util module
        temp = self._in_use.copy()
        temp.reverse()
        idx = (self._in_use.length() - 1) - temp.index(1)

        return self._get_zk_index_from_bit(idx)

    @classmethod
    def delete_all(cls, zookeeper_client, path):
        try:
            zookeeper_client.delete_node(path, recursive=True)
        except kazoo.exceptions.NotEmptyError:
            #TODO: Add retries for NotEmptyError
            zookeeper_client.syslog("NotEmptyError while deleting %s" % path)
    # end delete_all

#end class IndexAllocator

class ZookeeperCounter(Counter):

    def __init__(self, client, path, max_count=sys.maxsize, default=0):

        super(ZookeeperCounter, self).__init__(client, path, default)

        self.max_count = max_count
        # Delete existing counter if it exists with stale data
        if client.exists(path):
            data = client.get(path)[0]
            if data != b'':
                try:
                    self.default_type(data)
                except (TypeError, ValueError):
                    client.delete(path)
        self._ensure_node()

    def _inner_change(self, value):
        data, version = self._value()
        # Decrement counter only if data(current count) is non zero.
        if data > 0 or value > 0:
            data += value
        # Dont raise OverQuota during delete
        if (data > self.max_count and value > 0):
            raise OverQuota()
        try:
            self.client.set(
                    self.path, repr(data).encode('ascii'), version=version)
        except kazoo.exceptions.BadVersionError:  # pragma: nocover
            raise ForceRetryError()
# end class ZookeeperCounter

class ZookeeperClient(object):

    def __init__(self, module, server_list, host_ip, logging_fn=None, zk_timeout=400,
                 log_response_time=None):
        self.host_ip = host_ip
        # logging
        logger = logging.getLogger(module)
        logger.setLevel(logging.DEBUG)
        try:
            handler = logging.handlers.RotatingFileHandler(
                LOG_DIR + module + '-zk.log', maxBytes=10*1024*1024, backupCount=5)
        except IOError:
            print("Cannot open log file in %s" %(LOG_DIR))
        else:
            log_format = logging.Formatter('%(asctime)s [%(name)s]: %(message)s',
                                           datefmt='%m/%d/%Y %I:%M:%S %p')
            handler.setFormatter(log_format)
            logger.addHandler(handler)

        if logging_fn:
            self.log = logging_fn
        else:
            self.log = self.syslog
        self.log_response_time = log_response_time
        # KazooRetry to retry keeper CRUD operations
        self._retry = KazooRetry(max_tries=None, max_delay=300,
                                 sleep_func=gevent.sleep)
        self._zk_client = kazoo.client.KazooClient(
                server_list,
                timeout=zk_timeout,
                handler=kazoo.handlers.gevent.SequentialGeventHandler(),
                logger=logger,
                connection_retry=self._retry,
                command_retry=self._retry)

        self._zk_client.add_listener(self._zk_listener)
        self._logger = logger
        self._election = None
        self._server_list = server_list

        self._conn_state = None
        self._sandesh_connection_info_update(status='INIT', message='')
        self._lost_cb = None
        self._suspend_cb = None

        self.delete_node = self._response_time(self.delete_node, "DELETE")
        self.create_node = self._response_time(self.create_node, "CREATE")
        self.read_node = self._response_time(self.read_node, "READ")
        self.get_children= self._response_time(self.get_children, "GET_CHILDREN")
        self.exists = self._response_time(self.exists, "EXISTS")
        self.connect()
    # end __init__

    def _response_time(self, func, oper):
        def wrapper(*args, **kwargs):
            # Measure the time
            self.start_time = datetime.datetime.now()
            val = func(*args, **kwargs)
            self.end_time = datetime.datetime.now()
            if self.log_response_time:
                self.log_response_time(self.end_time - self.start_time, oper)
            return val
            # Measure the time again
        return wrapper

    # start
    def connect(self):
        while True:
            try:
                self._zk_client.start()
                break
            except gevent.event.Timeout as e:
                self._zk_client.close()
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                gevent.sleep(1)
            # Zookeeper is also throwing exception due to delay in master election
            except Exception as e:
                self._zk_client.stop()
                self._zk_client.close()
                # Update connection info
                self._sandesh_connection_info_update(status='DOWN',
                                                     message=str(e))
                gevent.sleep(1)
        # Update connection info
        self._sandesh_connection_info_update(status='UP', message='')

    # end

    def stop(self):
        self._zk_client.stop()
        self._zk_client.close()
    # end stop

    def is_connected(self):
        return self._zk_client.state == KazooState.CONNECTED
    # end is_connected

    def syslog(self, msg, *args, **kwargs):
        if not self._logger:
            return
        level = kwargs.get('level', 'info')
        if isinstance(level, int):
            from pysandesh.sandesh_logger import SandeshLogger
            level = SandeshLogger.get_py_logger_level(level)
            self._logger.log(level, msg)
            return

        log_method = getattr(self._logger, level, self._logger.info)
        log_method(msg)
    # end syslog

    def set_lost_cb(self, lost_cb=None):
        # set a callback to be called when kazoo state is lost
        # set to None for default action
        self._lost_cb = lost_cb
    # end set_lost_cb

    def set_suspend_cb(self, suspend_cb=None):
        # set a callback to be called when kazoo state is suspend
        # set to None for default action
        self._suspend_cb = suspend_cb
    # end set_suspend_cb

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
            self._sandesh_connection_info_update(status='DOWN',
                                      message='Connection to Zookeeper lost')
            if self._lost_cb:
                self._lost_cb()
            else:
                os._exit(2)
        elif state == KazooState.SUSPENDED:
            # Update connection info
            self._sandesh_connection_info_update(status='INIT',
                message = 'Connection to zookeeper lost. Retrying')
            if self._suspend_cb:
                self._suspend_cb()

    # end

    def master_election(self, path, identifier, func, *args, **kwargs):
        self._election = self._zk_client.Election(path, identifier)
        self._election.run(func, *args, **kwargs)
    # end master_election

    def quota_counter(self, path, max_count=sys.maxsize, default=0):
        return ZookeeperCounter(self._zk_client, path, max_count,
                                default=default)

    def create_node(self, path, value=None, ephemeral=False):
        try:
            if value is None:
                value = uuid.uuid4()
            retry = self._retry.copy()
            retry(self._zk_client.create, path, native_str(value),
                  ephemeral=ephemeral, makepath=True)
        except kazoo.exceptions.NodeExistsError:
            current_value = self.read_node(path)
            if current_value == value:
                return True
            raise ResourceExistsError(path, str(current_value), 'zookeeper')
    # end create_node

    def delete_node(self, path, recursive=False):
        try:
            retry = self._retry.copy()
            retry(self._zk_client.delete, path, recursive=recursive)
        except kazoo.exceptions.NoNodeError:
            pass

    # end delete_node

    def read_node(self, path, include_timestamp=False):
        try:
            retry = self._retry.copy()
            value = retry(self._zk_client.get, path)
            if include_timestamp:
                return value
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

    def exists(self, path):
        try:
            retry = self._retry.copy()
            return retry(self._zk_client.exists, path)
        except Exception:
            return []
    # end exists

    def _sandesh_connection_info_update(self, status, message):
        from pysandesh.connection_info import ConnectionState
        from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
        from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
        from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

        new_conn_state = getattr(ConnectionStatus, status)
        ConnectionState.update(conn_type = ConnType.ZOOKEEPER,
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

    def lock(self, path, identifier=None):
        if not identifier:
            identifier = '%s-%s' % (socket.getfqdn(self.host_ip),
                    os.getpid())
        return self._zk_client.Lock(path, identifier)

    def read_lock(self, path, identifier=None):
        if not identifier:
            identifier = '%s-%s' % (socket.getfqdn(self.host_ip),
                    os.getpid())
        return self._zk_client.ReadLock(path, identifier)

    def write_lock(self, path, identifier=None):
        if not identifier:
            identifier = '%s-%s' % (socket.getfqdn(self.host_ip),
                    os.getpid())
        return self._zk_client.WriteLock(path, identifier)
