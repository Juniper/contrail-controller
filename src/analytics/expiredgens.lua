--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

redis.log(redis.LOG_NOTICE,"Expired Gens ")
local gens = redis.call('smembers',"GENERATORS")
local res = {}
for k,v in pairs(gens) do
	local lgen = redis.call('exists', "GENERATOR:"..v)
	local sep = ":"
	local src = v:match(("([^"..sep.."]*)"..sep))
	local mod = v:match((sep.."([^"..sep.."]*)"))
	if lgen == 0 then
		table.insert(res, src)
		table.insert(res, mod)
	end
	redis.log(redis.LOG_NOTICE,"val for "..v.." is "..lgen)
end
return res
