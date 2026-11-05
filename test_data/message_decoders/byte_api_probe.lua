-- GPL-3.0-or-later. Synthetic Wireshark API probe; no protocol implementation.
local probe = Proto("cilogg_byte_api_probe", "CILogg byte API probe")
local fields = {
    string_length = ProtoField.uint32("cilogg_byte_api_probe.string_length", "String length"),
    raw_length = ProtoField.uint32("cilogg_byte_api_probe.raw_length", "Raw length"),
    raw_hex = ProtoField.string("cilogg_byte_api_probe.raw_hex", "Raw hexadecimal bytes"),
    can_type = ProtoField.string("cilogg_byte_api_probe.can_type", "Reassembly capability type"),
    can_value = ProtoField.string("cilogg_byte_api_probe.can_value", "Reassembly capability value"),
}
probe.fields = fields

function probe.dissector(tvb, pinfo, tree)
    local root = tree:add(probe, tvb())
    local raw = tvb():raw()
    local hex = {}
    for index = 1, #raw do
        hex[index] = string.format("%02x", string.byte(raw, index))
    end
    root:add(fields.string_length, #tvb():string()):set_generated()
    root:add(fields.raw_length, #raw):set_generated()
    root:add(fields.raw_hex, table.concat(hex)):set_generated()
    root:add(fields.can_type, type(pinfo.can_desegment)):set_generated()
    root:add(fields.can_value, tostring(pinfo.can_desegment)):set_generated()
    return tvb:len()
end
