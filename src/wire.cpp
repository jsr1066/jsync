#include "wire.h"

#include <cerrno>
#include <cstdio>
#include <string>

#include <spdlog/spdlog.h>

namespace jsync_ns
{
    namespace
    {
        std::unexpected<Error> fail(std::string message, int code)
        {
            spdlog::error("wire: {} (code {})", message, code);
            return std::unexpected(Error{std::move(message), code});
        }

        // Appends "key=value;". The value must not contain the delimiters.
        ExpectBool append_kv(std::string& body, const std::string& key, const std::string& value)
        {
            if (key.empty() || key.find_first_of(";=\n") != std::string::npos)
            {
                return fail("invalid key: " + key, EINVAL);
            }
            if (value.find_first_of(";\n") != std::string::npos)
            {
                return fail("invalid character in value of " + key + ": " + value, EINVAL);
            }
            body += key;
            body += '=';
            body += value;
            body += ';';
            return true;
        }

        // Writes the JS1.0 header followed by body into out.
        ExpectBool frame(const std::string& body, std::vector<uint8_t>& out)
        {
            if (body.size() > MAX_COMMAND_LEN)
            {
                return fail("command too long: " + std::to_string(body.size()), EMSGSIZE);
            }
            char header[32];
            int n = std::snprintf(header, sizeof header, "JS1.0;%08zu;", body.size());
            out.assign(header, header + n);
            out.insert(out.end(), body.begin(), body.end());
            return true;
        }
    }

    ExpectBool convertToWire(const ScanRequest& req, std::vector<uint8_t>& out)
    {
        std::string body;
        if (auto r = append_kv(body, "cmd", "REQ-SCAN"); !r)
        {
            return r;
        }
        if (auto r = append_kv(body, "directory", req.directory); !r)
        {
            return r;
        }
        if (!req.excludes.empty())
        {
            std::string joined;
            bool first = true;
            for (const std::string& rx : req.excludes)
            {
                if (rx.find(EXCLUDE_SEP) != std::string::npos)
                {
                    return fail("exclude contains reserved separator: " + rx, EINVAL);
                }
                if (!first)
                {
                    joined += EXCLUDE_SEP;
                }
                first = false;
                joined += rx;
            }
            if (auto r = append_kv(body, "excludes", joined); !r)
            {
                return r;
            }
        }
        return frame(body, out);
    }

    ExpectBool convertToWire(const ScanInfo& info, std::vector<uint8_t>& out)
    {
        std::string body;
        for (auto r : {
                 append_kv(body, "cmd", "RSP-SCAN"),
                 append_kv(body, "file", info.rpath),
                 append_kv(body, "size", std::to_string(info.size)),
                 append_kv(body, "mtime", std::to_string(static_cast<long long>(info.mtime))),
                 append_kv(body, "permission", std::to_string(info.permission)),
             })
        {
            if (!r)
            {
                return r;
            }
        }
        return frame(body, out);
    }
}
