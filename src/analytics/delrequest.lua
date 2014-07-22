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
redis.log(redis.LOG_NOTICE,"DelRequest for "..sm)
local db = tonumber(ARGV[5])
redis.call('select',db)
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
        local deltbl = string.sub(deluve, 1, st-1)

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
redis.call('srem', "NGENERATORS", sm)
redis.log(redis.LOG_NOTICE,"Delete Request for "..sm.." successful")
return true
