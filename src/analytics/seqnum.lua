--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

redis.log(redis.LOG_NOTICE,"GetSeq for "..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])
local db = tonumber(ARGV[5])
redis.call('select',db)
local typ = redis.call('smembers',"TYPES:"..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])
local res = {}
for k,v in pairs(typ) do
    local lres = redis.call('zrange',"UVES:"..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4]..":"..v, -1, -1, "withscores")
    if #lres == 2 then 
	    table.insert(res,v)
	    table.insert(res,lres[2])
	else
		redis.log(redis.LOG_NOTICE,"GetSeq no seq for "..v)
	end
end
redis.call('sadd', "NGENERATORS", ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])
return res
