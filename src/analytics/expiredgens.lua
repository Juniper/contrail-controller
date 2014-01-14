--
-- Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
--

redis.log(redis.LOG_NOTICE,"Expired Gens ")
local gens = redis.call('smembers',"NGENERATORS")
local res = {}
for k,v in pairs(gens) do
	local lgen = redis.call('exists', "NGENERATOR:"..v)
	local sep = ":"
        local vsep = v..sep
	local src, node, mod, inst = vsep:match(("([^"..sep.."]*)"..sep):rep(4))
	if lgen == 0 then
		table.insert(res, src)
		table.insert(res, node)
                table.insert(res, mod)
                table.insert(res, inst)
	end
	redis.log(redis.LOG_NOTICE,"val for "..v.." is "..lgen)
end
return res
