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
                redis.call('del',desc.href)
                redis.log(redis.LOG_NOTICE,"Deleting for "..desc.href)
            end
            redis.call('hdel', _values, attr)
        end 
        iter = iter + 2
    end
end

local sm = ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4] 
redis.log(redis.LOG_NOTICE,"DelRequest for "..sm.." vizd "..ARGV[5].." timeout "..ARGV[6])

-- conditional delete :
--     only delete if this generator is not owned by collector
if ARGV[6] == "-1" then
    local lttl = redis.call('exists', "NGENERATOR:"..sm)
    if lttl == 1 then
        redis.log(redis.LOG_NOTICE,"DelRequest failed for "..sm)
        return false
    end
end

local typ = redis.call('smembers',"TYPES:"..sm)

for k,v in pairs(typ) do
    local lres = redis.call('zrange',"UVES:"..sm..":"..v, 0, -1, "withscores")
    local iter = 1
    while iter <= #lres do
        local deltyp = v
        local deluve = lres[iter]
        local delseq = lres[iter+1]
        local st,en
        st,en = string.find(deluve,":")
        local deltbl = string.sub(deluve, st-1)

        local dkey = "DEL:"..deluve..":"..sm..":"..deltyp..":"..delseq
        redis.log(redis.LOG_NOTICE,"DEL for "..dkey)

        local dval = "VALUES:"..deluve..":"..sm..":"..deltyp
        sub_del(dval)

        local lttt = redis.call('exists', dval)
        if lttt == 1 then
            redis.call('rename', dval, dkey)
        end

        dval = "UVES:"..sm..":"..deltyp
        redis.call('zrem', dval, deluve)

        dval = "ORIGINS:"..deluve
        redis.call('srem', dval, sm..":"..deltyp)

        dval = "TABLE:"..deltbl
        redis.call('srem', dval, deluve..":"..sm..":"..deltyp)

        if lttt == 1 then
            redis.call('lpush',"DELETED", dkey)
        end 
        iter = iter + 2
    end
end

redis.call('del', "TYPES:"..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])

-- A collector is taking ownership of this generator
if #ARGV[5] >= 1 then
    redis.call('sadd', "NGENERATORS", sm)
    if ARGV[6] ~= "0" and ARGV[6] ~= "-1" then
        redis.call('set', "NGENERATOR:"..sm, ARGV[5], "EX", ARGV[6])
    else
        redis.call('set', "NGENERATOR:"..sm, ARGV[5])
    end
-- This generator is not owned by any collector
else
    redis.call('srem', "NGENERATORS", sm)
    redis.call('del', "NGENERATOR:"..sm)
end
return true
