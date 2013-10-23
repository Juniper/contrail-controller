#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import redis
import gevent

#
# Redis Sentinel Client
#


class RedisSentinelClient(object):
    def __init__(self, sentinel_info, services, master_change_event_handler,
                 logger):
        self._sentinel_info = sentinel_info
        self._services = services
        self._master_change_event_handler = master_change_event_handler
        self._logger = logger
        self._sentinel = None
        self._redis_masters = {}
        self._connect()
        gevent.spawn(self.sentinel_listener)
    #end __init__

    def _connect(self):
        try:
            self._sentinel = redis.StrictRedis(self._sentinel_info[0],
                                               self._sentinel_info[1])
            self._sentinel.execute_command('PING')
        except redis.exceptions.ConnectionError:
            self._sentinel = None
    #end _connect

    def _execute_sentinel_command(self, *args, **kwargs):
        if self._sentinel:
            try:
                return self._sentinel.execute_command('SENTINEL', *args,
                                                      **kwargs)
            except redis.exceptions.ConnectionError:
                return -1
        return None
    #end _execute_sentinel_command

    def _get_redis_master_from_sentinel(self, service):
        try:
            host, port =\
                self._execute_sentinel_command('get-master-addr-by-name',
                                               service)
            redis_master = (host, int(port))
        except Exception as e:
            redis_master = None
            self._logger.error(
                'Failed to get redis master for service "%s" from sentinel'
                % (service))
        finally:
            self._logger.info('Redis master for service %s is %s:%d'
                              % (service, redis_master[0], redis_master[1]))
            self._update_redis_master(service, redis_master)
            return redis_master
    #end _get_redis_master_from_sentinel

    def _get_redis_masters_from_sentinel(self):
        for service in self._services:
            self._get_redis_master_from_sentinel(service)
    #end _get_redis_masters_from_sentinel

    def _update_redis_master(self, service, redis_master):
        try:
            old_redis_master = self._redis_masters[service]
        except KeyError:
            old_redis_master = None
        finally:
            if old_redis_master != redis_master:
                if old_redis_master is not None:
                    del self._redis_masters[service]
                if redis_master is not None:
                    self._redis_masters[service] = redis_master
                    self._logger.info(
                        'Update Redis master %s:%d for service "%s"'
                        % (redis_master[0], redis_master[1], service))
                self._master_change_event_handler(service, redis_master)
            else:
                self._logger.info('No change in Redis master for service %s'
                                  % (service))
    #end _update_redis_master

    # public functions

    def get_redis_master(self, service):
        try:
            redis_master = self._redis_masters[service]
        except KeyError:
            return None
        else:
            return redis_master
    #end get_redis_master

    def sentinel_listener(self):
        while True:
            while self._sentinel is None:
                gevent.sleep(2)
                self._connect()
            self._logger.info('Connected to sentinel %s:%d'
                              % (self._sentinel_info[0],
                                 self._sentinel_info[1]))
            # update redis masters and trigger callback, if there is
            # a change in redis mastership for any service
            self._get_redis_masters_from_sentinel()
            pubsub = self._sentinel.pubsub()
            pubsub.psubscribe('*')
            while True:
                try:
                    msg = pubsub.listen().next()
                except Exception as err:
                    self._logger.error('Error reading message from sentinel')
                    self._sentinel = None
                    break
                else:
                    self._logger.debug('Sentinel message: %s' % (str(msg)))
                    if (msg['channel'] == '-sdown' or
                            msg['channel'] == '-odown'):
                        data = msg['data'].split()
                        if len(data) == 0:
                            self._logger.error(
                                'Failed to decode sentinel message')
                            continue
                        # Ignore state change of non-master
                        if data[0] == 'master':
                            # the format of channels "-sdown" and "-odown" for
                            # redis master is:
                            # master <master-name> <ip> <port>
                            if len(data) != 4:
                                self._logger.error(
                                    'Failed to decode sentinel message')
                                continue
                            self._logger.info(
                                'Redis master up for service %s [%s:%s]'
                                % (data[1], data[2], data[3]))
                            if data[1] in self._services:
                                redis_master = (data[2], int(data[3]))
                                self._update_redis_master(data[1],
                                                          redis_master)
                    # the channel 'switch-master' and 'redirect-to-master' is
                    # of the format:
                    # <master-name> <oldip> <old-port> <new-ip> <new-port>
                    elif (msg['channel'] == '+switch-master' or
                            msg['channel'] == '+redirect-to-master'):
                        data = msg['data'].split()
                        if len(data) != 5:
                            self._logger.error(
                                'Failed to decode sentinel message')
                            continue
                        self._logger.info(
                            'Redis master change for service %s' +
                            '[%s:%s => %s:%s]'
                            % (data[0], data[1], data[2], data[3], data[4]))
                        if data[0] in self._services:
                            redis_master = (data[3], int(data[4]))
                            self._update_redis_master(data[0], redis_master)
    #end sentinel_listerner

#end RedisSentinelClient
