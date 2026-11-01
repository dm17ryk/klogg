-- SPDX-License-Identifier: GPL-3.0-or-later
-- Synthetic two-byte PDUs over invented UDP traffic; not a protocol implementation.
local probe = Proto("cilogg_md01_reference", "CILogg reference comparison fixture")
local item = ProtoField.uint8("cilogg_md01_reference.item", "Repeated byte", base.DEC)
local text = ProtoField.string("cilogg_md01_reference.text", "Generated Unicode text")
local transformed = ProtoField.bytes("cilogg_md01_reference.transformed", "Transformed source")
probe.fields = { item, text, transformed }

function probe.dissector(tvb, pinfo, tree)
    for offset = 0, tvb:len() - 2, 2 do
        local root = tree:add(probe, tvb(offset, 2))
        root:add(item, tvb(offset, 1))
        root:add(item, tvb(offset + 1, 1))
        root:add(text, "שלום 😀"):set_generated()
        root:add(transformed, ByteArray.new("4142"):tvb("Reference transformed source")())
    end
    pinfo.cols.protocol = "MD01REF"
    return tvb:len()
end

DissectorTable.get("udp.port"):add(45679, probe)

-- Deliberately carrier-dependent: comparison must expose missing transport context.
local context = Proto("cilogg_md01_context", "CILogg carrier context fixture")
local port = ProtoField.string("cilogg_md01_context.port", "Source transport port")
context.fields = { port }
function context.dissector(tvb, pinfo, tree)
    local root = tree:add(context, tvb())
    root:add(port, tostring(pinfo.src_port)):set_generated()
    return tvb:len()
end
DissectorTable.get("udp.port"):add(45680, context)
