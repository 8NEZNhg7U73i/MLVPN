mlvpn_proto = Proto("MLVPN", "Multi-Link-VPN")
data_length = ProtoField.uint16("mlvpn.data_length", "Data Length", base.DEC)
version = ProtoField.uint16("mlvpn.version", "Version", base.DEC, nil, 0x000f)
type = ProtoField.uint16("mlvpn.type", "Type", base.DEC, {
	[0] = "MLVPN_PKT_AUTH",
	[1] = "MLVPN_PKT_AUTH_OK",
	[2] = "MLVPN_PKT_KEEPALIVE",
	[3] = "MLVPN_PKT_DATA",
	[4] = "MLVPN_PKT_DISCONNECT",
}, 0x03f0)
reorder = ProtoField.uint16("mlvpn.reorder", "Reorder", base.DEC, {
	[0] = "false",
	[1] = "true",
}, 0x0400)
reserved = ProtoField.uint16("mlvpn.reserved", "Reserved", base.DEC, nil, 0xf800)
timestamp = ProtoField.uint16("mlvpn.timestamp", "Timestamp", base.DEC)
timestamp_reply = ProtoField.uint16("mlvpn.reply_timestamp", "Reply Timestamp", base.DEC)
flow_id = ProtoField.uint32("mlvpn.flow_id", "Flow ID", base.HEX)
seq = ProtoField.uint64("mlvpn.seq", "Sequence number per flow", base.DEC)
data_seq = ProtoField.uint64("mlvpn.data_seq", "Global sequence number", base.DEC)
data = ProtoField.bytes("mlvpn.data", "Data")

mlvpn_proto.fields = {
	data_length,
	version,
	type,
	reorder,
	reserved,
	timestamp,
	timestamp_reply,
	flow_id,
	seq,
	data_seq,
	data,
}

datalen_field = Field.new("mlvpn.data_length")
type_field = Field.new("mlvpn.type")
version_field = Field.new("mlvpn.version")
flowid_field = Field.new("mlvpn.flow_id")
seq_field = Field.new("mlvpn.seq")
dataseq_field = Field.new("mlvpn.data_seq")

function mlvpn_proto.dissector(buffer, pinfo, tree)
	length = buffer:len()
	if length == 0 then return end
	pinfo.cols.protocol = mlvpn_proto.name
	local subtree = tree:add(mlvpn_proto, buffer(), "Multi-Link-VPN Protocol Data")
	
	subtree:add(data_length, buffer(0, 2))
	
	bitfield_subtree = subtree:add(buffer(2, 2), "Flags Bitfield")
	bitfield_subtree:add_le(version, buffer(2, 2))
	bitfield_subtree:add_le(type, buffer(2, 2))
	bitfield_subtree:add_le(reorder, buffer(2, 2))
	bitfield_subtree:add_le(reserved, buffer(2, 2))
	
	subtree:add(timestamp, buffer(4, 2))
	
	subtree:add(timestamp_reply, buffer(6, 2))
	
	subtree:add(flow_id, buffer(8, 4))
	
	subtree:add(seq, buffer(12, 8))
	
	subtree:add(data_seq, buffer(20, 8))
	
	subtree:add(data, buffer(28))
	
	--pinfo.cols.info:set("MLVPN")
	--pinfo.cols.info:set("Version="..tostring(version_field()))
	pinfo.cols.info:set(tostring(type_field().display))
	pinfo.cols.info:append(" Len="..tostring(datalen_field()))
	pinfo.cols.info:append(" DataSeq="..tostring(dataseq_field()))
	pinfo.cols.info:append(" FlowID="..tostring(flowid_field()))
	pinfo.cols.info:append(" FlowSeq="..tostring(seq_field()))
end

-- register our protocol to handle udp port 50252, 50253 and 50254
udp_table = DissectorTable.get("udp.port")
udp_table:add(50254, mlvpn_proto)
udp_table:add(50253, mlvpn_proto)
udp_table:add(50252, mlvpn_proto)
