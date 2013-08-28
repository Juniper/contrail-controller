--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

redis.log(redis.LOG_NOTICE,"GetSeq for "..ARGV[1]..":"..ARGV[2].." Vizd "..ARGV[3])
local typ = redis.call('smembers',"TYPES:"..ARGV[1]..":"..ARGV[2])
local res = {}
for k,v in pairs(typ) do
    local lres = redis.call('zrange',"UVES:"..ARGV[1]..":"..ARGV[2]..":"..v, -1, -1, "withscores")
    if #lres == 2 then 
	    table.insert(res,v)
	    table.insert(res,lres[2])
	else
		redis.log(redis.LOG_NOTICE,"GetSeq no seq for "..v)
	end
end
redis.call('sadd', "GENERATORS", ARGV[1]..":"..ARGV[2])
if ARGV[4] ~= "0" then
    redis.call('set', "GENERATOR:"..ARGV[1]..":"..ARGV[2],ARGV[3],"EX",ARGV[4])
else
    redis.call('set', "GENERATOR:"..ARGV[1]..":"..ARGV[2],ARGV[3])
end
return res
