local dprint2 = function() end
dprint2 = function(...)
    print(table.concat({"Lua:", ...}," "))
end
----------------------------------------
-- creates a Proto object, but doesn't register it yet
local mpls_udp = Proto("mpls_udp","MplsoUdp Protocol")

----------------------------------------
-- create a protocol field (but not register it yet)
-- the abbreviation should always have "<myproto>." before the specific abbreviation, to avoid collisions
local pf_mpls_label            = ProtoField.new   ("Mpls Label", "mpls_udp.mpls_label", ftypes.UINT32)

----------------------------------------
-- this actually registers the ProtoFields above, into our new Protocol
mpls_udp.fields = { pf_mpls_label }

----------------------------------------
-- The following creates the callback function for the dissector.
-- It's the same as doing "dns.dissector = function (tvbuf,pkt,root)"
-- The 'tvbuf' is a Tvb object, 'pktinfo' is a Pinfo object, and 'root' is a TreeItem object.
-- Whenever Wireshark dissects a packet that our Proto is hooked into, it will call
-- this function and pass it these arguments for the packet it's dissecting.
function mpls_udp.dissector(tvbuf,pktinfo,root)

    -- set the protocol column to show our protocol name
    pktinfo.cols.protocol:set("MplsoUdp")

    -- We start by adding our protocol to the dissection display tree.
    -- A call to tree:add() returns the child created, so we can add more "under" it using that return value.
    -- The second argument is how much of the buffer/packet this added tree item covers/represents
    local tree = root:add(mpls_udp, tvbuf:range(0,4))

    -- Now let's add our mpls label under our MplsoUdp protocol tree we just created.
    -- The label is present in first 20 bits of MPLS header
    local mpls_field = tvbuf:range(0,4):uint()
    tree:add(pf_mpls_label, bit.rshift(mpls_field,12))
   
    -- Now check if inner packet is layer-2 
    local inner_type = tvbuf:range(16,2):uint()
    local version = tvbuf:range(18,1):uint()
    version = bit.rshift(version,4)
    -- Check if it is Arp Ethertype
    if inner_type == 2054 then
        Dissector.get("arp"):call(tvbuf(18):tvb(),pktinfo,root)
        return
    -- Check if it is Ip Ethertype
    elseif inner_type == 2048 and version == 4 then
        Dissector.get("ip"):call(tvbuf(18):tvb(),pktinfo,root)
        return
    -- Check if it is Ipv6 Ethertype
    elseif inner_type == 34525 and version == 6 then
        Dissector.get("ipv6"):call(tvbuf(18):tvb(),pktinfo,root)
        return
    else
    -- potentially a layer-3 packet, check for version field
        version = tvbuf:range(4,1):uint()
        version = bit.rshift(version,4)
        if version == 4 then
            Dissector.get("ip"):call(tvbuf(4):tvb(),pktinfo,root)
        elseif version == 6 then
            Dissector.get("ipv6"):call(tvbuf(4):tvb(),pktinfo,root)
        else
        -- Otherwise call data dissector which prints payload data
            local data_dis = Dissector.get("data")
            data_dis:call(tvbuf(4):tvb(),pktinfo,root)
        end
    end
end

----------------------------------------
-- we want to have our protocol dissection invoked for a specific UDP port,
-- so get the udp dissector table and add our protocol to it
DissectorTable.get("udp.port"):add(51234, mpls_udp)

----------------------------------------
-- creates a Proto object, but doesn't register it yet
local mpls_gre = Proto("mpls_gre","MplsoGre Protocol")

----------------------------------------
-- create a protocol field (but not register it yet)
-- the abbreviation should always have "<myproto>." before the specific abbreviation, to avoid collisions
local pf_gre_flags             = ProtoField.new   ("Gre flags", "mpls_gre.gre_flags", ftypes.UINT16)
local pf_gre_protocol          = ProtoField.new   ("Gre proto", "mpls_gre.gre_protocol", ftypes.UINT16)
local pf_mpls_label            = ProtoField.new   ("Mpls Label", "mpls_gre.mpls_label", ftypes.UINT32)

----------------------------------------
-- this actually registers the ProtoFields above, into our new Protocol
mpls_gre.fields = { pf_gre_flags, pf_gre_protocol, pf_mpls_label }

----------------------------------------
-- The following creates the callback function for the dissector.
-- It's the same as doing "dns.dissector = function (tvbuf,pkt,root)"
-- The 'tvbuf' is a Tvb object, 'pktinfo' is a Pinfo object, and 'root' is a TreeItem object.
-- Whenever Wireshark dissects a packet that our Proto is hooked into, it will call
-- this function and pass it these arguments for the packet it's dissecting.
function mpls_gre.dissector(tvbuf,pktinfo,root)

    -- set the protocol column to show our protocol name
    pktinfo.cols.protocol:set("MplsoGre")

    -- We start by adding our protocol to the dissection display tree.
    -- A call to tree:add() returns the child created, so we can add more "under" it using that return value.
    -- The second argument is how much of the buffer/packet this added tree item covers/represents
    local tree = root:add(mpls_gre, tvbuf:range(0,8))

    -- Now let's add gre flags, protocol and mpls label under our MplsoGre protocol tree we just created.
    tree:add(pf_gre_flags, tvbuf:range(0,2))
    tree:add(pf_gre_protocol, tvbuf:range(2,2))
    local mpls_field = tvbuf:range(4,4):uint()
    tree:add(pf_mpls_label, bit.rshift(mpls_field,12))

    -- Now check if inner-packet is layer-2
    local inner_type = tvbuf:range(20,2):uint()
    local version = tvbuf:range(22,1):uint()
    version = bit.rshift(version,4)
    if inner_type == 2054 then
        Dissector.get("arp"):call(tvbuf(22):tvb(),pktinfo,root)
        return
    elseif inner_type == 2048 and version == 4 then
        Dissector.get("ip"):call(tvbuf(22):tvb(),pktinfo,root)
        return
    elseif inner_type == 34525 and version == 6 then
        Dissector.get("ipv6"):call(tvbuf(22):tvb(),pktinfo,root)
        return
    else
        -- Potentially a layer-3 packet
        version = tvbuf:range(8,1):uint()
        version = bit.rshift(version,4)
        if version == 4 then
            Dissector.get("ip"):call(tvbuf(8):tvb(),pktinfo,root)
        elseif version == 6 then
            Dissector.get("ipv6"):call(tvbuf(8):tvb(),pktinfo,root)
        else
            -- Call data-dissector with payload
            local data_dis = Dissector.get("data")
            data_dis:call(tvbuf(8):tvb(),pktinfo,root)
        end
    end
end

----------------------------------------
-- we want to have our protocol dissection invoked for a specific ip proto,
-- so get the ip proto dissector table and add our protocol to it
DissectorTable.get("ip.proto"):add(47, mpls_gre)
-- We're done!
-- our protocol (Proto) gets automatically registered after this script finishes loading
----------------------------------------
