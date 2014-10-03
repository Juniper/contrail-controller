--
-- Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
--

redis.log(redis.LOG_NOTICE,"WARNING: Flushing Redis UVE DB")
local db = tonumber(ARGV[1])
redis.call('select',db)
redis.call('flushdb')
