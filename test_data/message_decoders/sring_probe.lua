-- SPDX-License-Identifier: GPL-3.0-or-later
-- MD01 proof fixture only: strict single SRING envelope, real registered children.
-- No EHCP/EHCI semantics or AT conversation assembly is implemented here.
local CHILD_LIMIT = 65536
local WRAPPER_LIMIT = CHILD_LIMIT * 2 + 32

local function register_wrapper(name, child_name)
    local proto = Proto(name, "CILogg strict nested proof " .. child_name)
    local status = ProtoField.string(name .. ".status", "Envelope status")
    local child_status = ProtoField.string(name .. ".child_status", "Child invocation status")
    local declared = ProtoField.uint32(name .. ".declared_bytes", "Declared bytes", base.DEC)
    local actual = ProtoField.uint32(name .. ".actual_bytes", "Observed bytes", base.DEC)
    local hex_offset = ProtoField.uint32(name .. ".hex_offset", "Hex byte offset, zero based", base.DEC)
    local hex_chars = ProtoField.uint32(name .. ".hex_chars", "Hex characters", base.DEC)
    proto.fields = { status, child_status, declared, actual, hex_offset, hex_chars }

    function proto.dissector(tvb, pinfo, tree)
        local root = tree:add(proto, tvb())
        local function decision(envelope, child)
            -- Diagnostics never include wrapper/child bodies or display labels.
            io.stderr:write(string.format(
                "cilogg_nested protocol=%s bytes=%d envelope=%s child=%s\n",
                name, tvb:len(), envelope, child))
        end
        local function reject(reason)
            root:add(status, reason):set_generated()
            root:add(child_status, "not_invoked"):set_generated()
            decision(reason, "not_invoked")
            return tvb:len()
        end
        if tvb:len() > WRAPPER_LIMIT then
            return reject("wrapper_limit")
        end
        -- Header spacing is explicit; no payload whitespace, CRLF repair or STRING alias.
        local text = tvb():string()
        -- Wireshark's string conversion can stop at NUL. Never accept a prefix.
        if #text ~= tvb:len() then
            return reject("invalid_envelope")
        end
        local socket, count, first_hex, hex = text:match(
            "^SRING:[ \t]*(%d+),[ \t]*(%d+),[ \t]*()([%x]*)$")
        if not socket then
            return reject("invalid_envelope")
        end
        if #socket > 5 or tonumber(socket) > 65535 then
            return reject("socket_limit")
        end
        if #count > 5 or tonumber(count) > CHILD_LIMIT then
            return reject("declared_limit")
        end
        root:add(declared, tonumber(count)):set_generated()
        root:add(hex_offset, first_hex - 1):set_generated()
        root:add(hex_chars, #hex):set_generated()
        if #hex % 2 ~= 0 then
            return reject("odd_hex")
        end
        root:add(actual, #hex / 2):set_generated()
        if #hex / 2 ~= tonumber(count) then
            return reject("length_mismatch")
        end
        if #hex == 0 then
            return reject("empty_payload")
        end
        root:add(status, "valid"):set_generated()
        local ok, child = pcall(Dissector.get, child_name)
        if not ok or child == nil then
            root:add(child_status, "missing"):set_generated()
            decision("valid", "missing")
            return tvb:len()
        end
        -- Invoked is not an integrity/semantic success assertion for the child.
        root:add(child_status, "invoked"):set_generated()
        decision("valid", "invoking")
        local child_tvb = ByteArray.new(hex):tvb("MD01 nested hex child")
        child:call(child_tvb, pinfo, root)
        return tvb:len()
    end
end

register_wrapper("cilogg_md01_nested_data", "data")
register_wrapper("cilogg_md01_nested_ehcp", "ehcp")
register_wrapper("cilogg_md01_nested_missing", "cilogg_md01_intentionally_absent_child")
