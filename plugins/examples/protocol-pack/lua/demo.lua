-- A TShark dissector for the synthetic demo protocol: one type byte, one length byte, then the
-- payload. The "Demo frame" preview is routed to it (content.decoder_routes in plugin.json).
local demo = Proto("cilogg_demo", "CILogg demo protocol")

local field_type = ProtoField.uint8("cilogg_demo.type", "Type", base.DEC, DEMO_TYPES)
local field_length = ProtoField.uint8("cilogg_demo.length", "Length", base.DEC)
local field_payload = ProtoField.bytes("cilogg_demo.payload", "Payload")
demo.fields = { field_type, field_length, field_payload }

function demo.dissector(buffer, pinfo, tree)
    if buffer:len() < 2 then
        return 0
    end
    pinfo.cols.protocol = "DEMO"
    local subtree = tree:add(demo, buffer(), "CILogg demo frame")
    subtree:add(field_type, buffer(0, 1))
    subtree:add(field_length, buffer(1, 1))
    local length = buffer(1, 1):uint()
    if length > 0 and buffer:len() >= 2 + length then
        subtree:add(field_payload, buffer(2, length))
    end
    return buffer:len()
end
