--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

local sm = ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4] 
local ngen_sm = ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[6] 
redis.log(redis.LOG_NOTICE,"DelRequest for "..sm)
local db = tonumber(ARGV[5])
redis.call('select',db)
local typ = redis.call('smembers',"TYPES:"..sm)

local ism = redis.call('sismember', 'NGENERATORS', ngen_sm)
if ism == 0 then
    return false
end
redis.call('expire', "NGENERATORS", 40)

local res = {}
for k,v in pairs(typ) do
    redis.log(redis.LOG_NOTICE, "Read UVES:"..sm..":"..v)
    local lres = redis.call('zrange',"UVES:"..sm..":"..v, 0, -1, "withscores")
    redis.call('del', "UVES:"..sm..":"..v)
    local iter = 1
    local citer = 1
    redis.log(redis.LOG_NOTICE, "Delete "..sm..":"..v.." [#"..(#lres/2).."]")
    while iter <= #lres do
        local deltyp = v
        local deluve = lres[iter]
        local delseq = lres[iter+1]
        local st,en
        table.insert(res, deluve)
        table.insert(res, deltyp)
 
        st,en = string.find(deluve,":")
        local deltbl = string.sub(deluve, 1, st-1)

	local part = redis.call('hget',"KEY2PART:"..sm..":"..deltyp, deluve)
	if not part then
	   part = "NULL"
	else
	   redis.call('hdel', "KEY2PART:"..sm..":"..deltyp, deluve)
	   redis.call('srem', "PART2KEY:"..part, sm..":"..deltyp..":"..deluve)
	end

        local dval = "VALUES:"..deluve..":"..sm..":"..deltyp
        redis.call('del', dval)

        dval = "ORIGINS:"..deluve
        if redis.call('srem', dval, sm..":"..deltyp) == 1 then
            dval = "TABLE:"..deltbl
            redis.call('srem', dval, deluve..":"..sm..":"..deltyp)
        else
            dval = "ALARM_ORIGINS:"..deluve
            redis.call('srem', dval, sm..":"..deltyp)
            dval = "ALARM_TABLE:"..deltbl
            redis.call('srem', dval, deluve..":"..sm..":"..deltyp)
        end

        iter = iter + 2

        if citer > 100 then
            redis.call('expire', "NGENERATORS", 40)
            citer = 1
        else
            citer = citer + 2
        end
    end
end

redis.call('del', "TYPES:"..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])
redis.call('srem', "NGENERATORS", ngen_sm)
redis.log(redis.LOG_NOTICE,"Delete Request for "..sm.." successful")
return res
