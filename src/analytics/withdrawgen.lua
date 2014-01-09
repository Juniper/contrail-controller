--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

local coll = redis.call('get',"NGENERATOR:"..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])

if coll == ARGV[5] then
	redis.log(redis.LOG_NOTICE,"Withdraw Gen "..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4].." Collector "..ARGV[5])
	redis.call('del', "NGENERATOR:"..ARGV[1]..":"..ARGV[2]..":"..ARGV[3]..":"..ARGV[4])
	return true
end

return false
