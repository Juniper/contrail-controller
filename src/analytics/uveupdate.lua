--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

local sm = ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4]
local typ = ARGV[5]
local attr = ARGV[6]
local key = ARGV[7]
local seq = ARGV[8]
local val = ARGV[9]
local db = tonumber(ARGV[10])

local _types = KEYS[1]
local _origins = KEYS[2]
local _table = KEYS[3]
local _uves = KEYS[4]
local _values = KEYS[5]

redis.call('select',db)
redis.call('sadd',_types,typ)
redis.call('sadd',_origins,sm..":"..typ)
redis.call('sadd',_table,key..':'..sm..":"..typ)
redis.call('zadd',_uves,seq,key)
redis.call('hset',_values,attr,val)

return true
