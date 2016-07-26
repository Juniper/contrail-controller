local dprint2 = function() end
dprint2 = function(...)
    print(table.concat({"Lua:", ...}," "))
end
----------------------------------------
-- creates a Proto object, but doesn't register it yet
local agent = Proto("agent","agent Protocol")

----------------------------------------
-- multiple ways to do the same thing: create a protocol field (but not register it yet)
-- the abbreviation should always have "<myproto>." before the specific abbreviation, to avoid collisions
local pf_if_index              = ProtoField.new   ("Interface index", "agent.if_id", ftypes.UINT16)
local pf_vrf                   = ProtoField.new   ("Vrf", "agent.vrf", ftypes.UINT16)
local pf_cmd                   = ProtoField.new   ("command", "agent.cmd", ftypes.UINT16)
local pf_cmd_param             = ProtoField.new   ("command parameter", "agent.cmd_param", ftypes.UINT32)
local pf_cmd_param_1           = ProtoField.new   ("command parameter 1", "agent.cmd_param_1", ftypes.UINT32)
local pf_cmd_param_2           = ProtoField.new   ("command parameter 2", "agent.cmd_param_2", ftypes.UINT32)
local pf_cmd_param_3           = ProtoField.new   ("command parameter 3", "agent.cmd_param_3", ftypes.UINT32)
local pf_cmd_param_4           = ProtoField.new   ("command parameter 4", "agent.cmd_param_4", ftypes.UINT32)
local pf_cmd_param_5           = ProtoField.new   ("command parameter 5", "agent.cmd_param_5", ftypes.UINT32)

----------------------------------------
-- this actually registers the ProtoFields above, into our new Protocol
agent.fields = { pf_if_index, pf_vrf,
    pf_cmd, pf_cmd_param, pf_cmd_param_1, pf_cmd_param_2,
    pf_cmd_param_3, pf_cmd_param_4, pf_cmd_param_5 }

----------------------------------------
-- The following creates the callback function for the dissector.
-- It's the same as doing "dns.dissector = function (tvbuf,pkt,root)"
-- The 'tvbuf' is a Tvb object, 'pktinfo' is a Pinfo object, and 'root' is a TreeItem object.
-- Whenever Wireshark dissects a packet that our Proto is hooked into, it will call
-- this function and pass it these arguments for the packet it's dissecting.
function agent.dissector(tvbuf,pktinfo,root)

    -- set the protocol column to show our protocol name
    pktinfo.cols.protocol:set("Agent")

    -- We start by adding our protocol to the dissection display tree.
    -- A call to tree:add() returns the child created, so we can add more "under" it using that return value.
    -- The second argument is how much of the buffer/packet this added tree item covers/represents
    local tree = root:add(agent, tvbuf:range(0,30))
    local offset = 14

    -- Now let's add our if_index under our agent protocol tree we just created.
    -- The if_index starts at offset 0, for 2 bytes length.
    tree:add(pf_if_index, tvbuf:range(offset,2))
    
    -- now add more to the main agent tree
    tree:add(pf_vrf, tvbuf:range(offset+2,2))
    tree:add(pf_cmd, tvbuf:range(offset+4,2))
    tree:add(pf_cmd_param, tvbuf:range(offset+6,4))
    tree:add(pf_cmd_param_1, tvbuf:range(offset+10,4))
    tree:add(pf_cmd_param_2, tvbuf:range(offset+14,4))
    tree:add(pf_cmd_param_3, tvbuf:range(offset+18,4))
    tree:add(pf_cmd_param_4, tvbuf:range(offset+22,4))
    tree:add(pf_cmd_param_5, tvbuf:range(offset+26,4))

    local data_dis = Dissector.get("data")
    local inner_type = tvbuf:range(offset+42,2):uint()
    if inner_type == 2054 then
        Dissector.get("arp"):call(tvbuf(58):tvb(),pktinfo,root)
    elseif inner_type == 2048 then
        local ip_proto = tvbuf:range(67,1):uint()
        if ip_proto == 17 then
            local udp_port = tvbuf:range(80,2):uint()
            if udp_port == 51234 then
                -- It is a MPLSoUDP encapsulated packet
                -- Check if inner packet is Layer-2
                local ethertype = tvbuf:range(102,2):uint()
                local version = tvbuf:range(104,1):uint()
                version = bit.rshift(version,4)
                if ethertype == 2048 and version == 4 then
                    Dissector.get("ip"):call(tvbuf(104):tvb(),pktinfo,root)
                elseif ethertype == 34525 and version == 6 then
                    Dissector.get("ipv6"):call(tvbuf(104):tvb(),pktinfo,root)
                else
                    -- inner packet can potentially be layer-3
                    version = tvbuf:range(90,1):uint()
                    version = bit.rshift(version,4)
                    if version == 4 then
                        Dissector.get("ip"):call(tvbuf(90):tvb(),pktinfo,root)
                    elseif version == 6 then
                        Dissector.get("ipv6"):call(tvbuf(90):tvb(),pktinfo,root)
                    else
                        data_dis:call(tvbuf(90):tvb(),pktinfo,root)
                    end
                end
                return
            end
        elseif ip_proto == 47 then
            -- It is a MPLSoGRE encapsulated packet
            -- Check if inner packet is Layer-2
            local ethertype = tvbuf:range(98,2):uint()
            local version = tvbuf:range(100,1):uint()
            version = bit.rshift(version,4)
            if ethertype == 2048 and version == 4 then
                Dissector.get("ip"):call(tvbuf(100):tvb(),pktinfo,root)
            elseif ethertype == 34525 and version == 6 then
                Dissector.get("ipv6"):call(tvbuf(100):tvb(),pktinfo,root)
            else
                -- inner packet can potentially be layer-3
                version = tvbuf:range(86,1):uint()
                version = bit.rshift(version,4)
                if version == 4 then
                    Dissector.get("ip"):call(tvbuf(86):tvb(),pktinfo,root)
                elseif version == 6 then
                    Dissector.get("ipv6"):call(tvbuf(86):tvb(),pktinfo,root)
                else
                    data_dis:call(tvbuf(86):tvb(),pktinfo,root)
                end
            end
            return
        end 
        Dissector.get("ip"):call(tvbuf(58):tvb(),pktinfo,root)
    elseif inner_type == 34525 then
        Dissector.get("ipv6"):call(tvbuf(58):tvb(),pktinfo,root)
    else
        data_dis:call(tvbuf(44):tvb(),pktinfo,root)
    end
end

----------------------------------------
-- so get the wiretap-encap dissector table and add our protocol to it
local wtap_encap_table = DissectorTable.get("wtap_encap")
wtap_encap_table:add(1, agent)

-- We're done!
-- our protocol (Proto) gets automatically registered after this script finishes loading
----------------------------------------
