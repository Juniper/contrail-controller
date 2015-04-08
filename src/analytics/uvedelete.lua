--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

local function sub_del(_values)
    local lres = redis.call('hgetall',_values)
    local iter = 1
    while iter <= #lres do
        local attr = lres[iter]
        local val = lres[iter+1]
        if string.byte(val,1) ~= 60 then
            local descs = cjson.decode(val)
            for k,desc in pairs(descs) do
                if desc.href ~= nil then
                    redis.call('del',desc.href)
                    redis.log(redis.LOG_NOTICE,"Deleting for "..desc.href)
                end
            end
            redis.call('hdel', _values, attr)
        end 
        iter = iter + 2
    end
end

local sm = ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4]
local typ = ARGV[5]
local key = ARGV[6]
local db = tonumber(ARGV[7])
local is_alarm = tonumber(ARGV[8])

local _del = KEYS[1]
local _values = KEYS[2]
local _uves = KEYS[3]
local _origins = KEYS[4]
local _table = KEYS[5]
local _deleted = KEYS[6]

redis.call('select',db)

if is_alarm == 1 then
    redis.log(redis.LOG_NOTICE,"DelAlarm on "..sm.." for "..key)
else
    local part = redis.call('hget',"KEY2PART:"..sm..":"..typ, key)
    if part == false then
        part = "NULL"
    else
        redis.call('hdel', "KEY2PART:"..sm..":"..typ, key)
        redis.call('srem', "PART2KEY:"..part, sm..":"..typ..":"..key)
    redis.log(redis.LOG_NOTICE,"DelUVE on "..sm.." for "..key.." part "..part)
    end
end

sub_del(_values)

redis.call('zrem', _uves, key)
redis.call('srem', _origins, sm..":"..typ)
redis.call('srem', _table, key..":"..sm..":"..typ)

local lttt = redis.call('exists', _values)
if lttt == 1 then
    redis.call('rename', _values, _del)
    redis.call('lpush',_deleted, _del)
end
return true
