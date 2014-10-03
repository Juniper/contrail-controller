--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

local function histbin(hist,hval)
    --redis.log(redis.LOG_NOTICE,"histbin on "..hist.." "..hval)
    local space = hist
    for i = 1,4 do
        if hval <= (i*space) then
            return "b"..(i*space)
        end
    end
    local cur = hist*4
    for j = 1,6 do
        local val
        if space == math.ceil(space) then
            val = math.ceil(cur * 1.3)
        else
            val = cur * 1.3
        end
        if hval <= val then
            return "b"..val
        end
        cur = val
    end
    return "bmax"
end


local sm = ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4]
local typ = ARGV[5]
local attr = ARGV[6]
local key = ARGV[7]
local seq = ARGV[8]
local hist = tonumber(ARGV[9])
local tim = ARGV[10]
local val = tonumber(ARGV[11])
local db = tonumber(ARGV[12])

local _types = KEYS[1]
local _origins = KEYS[2]
local _table = KEYS[3]
local _uves = KEYS[4]
local _values = KEYS[5]
local _stats = KEYS[6]
local _s_3600 = KEYS[7]
local _s_3600p = KEYS[8]

redis.call('select',db)
redis.call('sadd',_types,typ)
redis.call('sadd',_origins,sm..":"..typ)
redis.call('sadd',_table,key..':'..sm..":"..typ)
redis.call('zadd',_uves,seq,key)
local desc1 = { aggtype="history-10", rtype='list', href=_stats}
local desc2 = { aggtype="s-3600-topvals", rtype='zset', href=_s_3600}
local desc3 = { aggtype="s-3600-summary", rtype='hash', href=_s_3600p}
redis.call('hset',_values,attr,cjson.encode({desc1,desc2,desc3}))
local dval = { [tim] = val }
redis.call('lpush',_stats,cjson.encode(dval))
redis.call('ltrim',_stats,0,9)
redis.call('zadd',_s_3600,val,tim)
redis.call('zremrangebyrank',_s_3600,0,-6)
redis.call('expire',_s_3600,3600)
redis.call('hincrbyfloat', _s_3600p, "sum",val)
redis.call('hincrby', _s_3600p, histbin(hist,val),1)

return true
