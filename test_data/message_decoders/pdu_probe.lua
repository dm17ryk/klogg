-- SPDX-License-Identifier: GPL-3.0-or-later
-- Synthetic MD01 engine fixture. No EHCP/EHCI semantic implementation.
local probe = Proto("cilogg_md01_probe", "CILogg MD01 synthetic engine probe")
local item = ProtoField.uint8("cilogg_md01_probe.item", "Repeated byte", base.DEC)
local generated = ProtoField.string("cilogg_md01_probe.generated", "Generated exact integer")
local transformed = ProtoField.bytes("cilogg_md01_probe.transformed", "Transformed source")
probe.fields = { item, generated, transformed }

function probe.dissector(tvb, pinfo, tree)
    local root = tree:add(probe, tvb())
    for i = 0, tvb:len() - 1 do
        root:add(item, tvb(i, 1))
    end
    root:add(generated, "9007199254740993"):set_generated()
    local decoded = ByteArray.new("4142"):tvb("MD01 transformed fixture")
    root:add(transformed, decoded())
    pinfo.cols.protocol = "MD01"
    return tvb:len()
end

-- Deliberately narrow synthetic SRING dialect. Real AT assembly is MD06 work.
local wrapper = Proto("cilogg_md01_sring", "CILogg MD01 synthetic SRING wrapper")
local length = ProtoField.uint32("cilogg_md01_sring.length", "Declared child bytes", base.DEC)
wrapper.fields = { length }

function wrapper.dissector(tvb, pinfo, tree)
    local count, hex = tvb():string():match("^SRING: 1,(%d+),([%x]+)$")
    if not count or #hex % 2 ~= 0 or #hex / 2 ~= tonumber(count) then
        return 0
    end
    local root = tree:add(wrapper, tvb())
    root:add(length, tonumber(count)):set_generated()
    local child = ByteArray.new(hex):tvb("MD01 SRING child")
    Dissector.get("ehcp"):call(child, pinfo, root)
    return tvb:len()
end
