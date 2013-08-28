--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

local coll = redis.call('get',"GENERATOR:"..ARGV[1]..":"..ARGV[2])

if coll == ARGV[3] then
	redis.log(redis.LOG_NOTICE,"Withdraw Gen "..ARGV[1]..":"..ARGV[2].." Collector "..ARGV[3])
	redis.call('del', "GENERATOR:"..ARGV[1]..":"..ARGV[2])
	return true
end

return false
