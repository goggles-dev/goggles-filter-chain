#pragma once

#include <cstdint>
#include <format>
#include <string>

namespace goggles::diagnostics {

/// Write-only JSON serializer that builds a JSON string in memory.
/// Comma insertion is managed automatically via a single boolean flag.
class JsonWriter {
public:
    void begin_object() {
        maybe_comma();
        m_buf += '{';
        m_needs_comma = false;
    }

    void end_object() {
        m_buf += '}';
        m_needs_comma = true;
    }

    void begin_array() {
        maybe_comma();
        m_buf += '[';
        m_needs_comma = false;
    }

    void end_array() {
        m_buf += ']';
        m_needs_comma = true;
    }

    void key(const std::string& k) {
        maybe_comma();
        write_escaped(k);
        m_buf += ':';
        m_needs_comma = false;
    }

    void value(int32_t v) {
        maybe_comma();
        m_buf += std::to_string(v);
        m_needs_comma = true;
    }

    void value(uint32_t v) {
        maybe_comma();
        m_buf += std::to_string(v);
        m_needs_comma = true;
    }

    void value(uint64_t v) {
        maybe_comma();
        m_buf += std::to_string(v);
        m_needs_comma = true;
    }

    void value(double v) {
        maybe_comma();
        // Use std::format with {:g} to get shortest-roundtrip representation
        // that drops trailing zeros (e.g. 3.14 not 3.140000).
        m_buf += std::format("{:g}", v);
        m_needs_comma = true;
    }

    void value(bool v) {
        maybe_comma();
        m_buf += v ? "true" : "false";
        m_needs_comma = true;
    }

    void value_string(const std::string& v) {
        maybe_comma();
        write_escaped(v);
        m_needs_comma = true;
    }

    void value_null() {
        maybe_comma();
        m_buf += "null";
        m_needs_comma = true;
    }

    [[nodiscard]] auto str() const -> const std::string& { return m_buf; }

private:
    void maybe_comma() {
        if (m_needs_comma) {
            m_buf += ',';
        }
    }

    void write_escaped(const std::string& s) {
        m_buf += '"';
        for (char c : s) {
            switch (c) {
            case '"':
                m_buf += "\\\"";
                break;
            case '\\':
                m_buf += "\\\\";
                break;
            case '\n':
                m_buf += "\\n";
                break;
            case '\r':
                m_buf += "\\r";
                break;
            case '\t':
                m_buf += "\\t";
                break;
            case '\b':
                m_buf += "\\b";
                break;
            case '\f':
                m_buf += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    m_buf += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                } else {
                    m_buf += c;
                }
                break;
            }
        }
        m_buf += '"';
    }

    std::string m_buf;
    bool m_needs_comma = false;
};

} // namespace goggles::diagnostics
