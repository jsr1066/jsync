#include "wire.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include <spdlog/spdlog.h>

namespace jsync_ns
{
    namespace
    {
        std::unexpected<Error> fail(std::string message, int code)
        {
            spdlog::error("wire: {} (code {})", message, code);
            return std::unexpected(Error{std::move(message), code, ErrorKind::Errno});
        }

        // Appends "key=value<FS>". The value must not contain the delimiters.
        ExpectBool append_kv(std::string& body, const std::string& key, const std::string& value)
        {
            if (key.empty() || key.find_first_of(std::string{FIELD_SEP} + "=\n") != std::string::npos)
            {
                return fail("invalid key: " + key, EINVAL);
            }
            if (value.find_first_of(std::string{FIELD_SEP} + "\n") != std::string::npos)
            {
                return fail("invalid character in value of " + key + ": " + value, EINVAL);
            }
            body += key;
            body += '=';
            body += value;
            body += FIELD_SEP;
            return true;
        }

        // Returns the value of a mandatory key, or null (and sets err) if absent or empty.
        const std::string* need(const KeyValues& kv, const char* key, std::string& err)
        {
            auto it = kv.find(key);
            if (it == kv.end() || it->second.empty())
            {
                err = std::string("missing ") + key;
                return nullptr;
            }
            return &it->second;
        }

        // Parses a whole-string decimal integer in [lo, hi].
        bool parse_int(const std::string& v, long long lo, long long hi, long long& out)
        {
            if (v.empty())
            {
                return false;
            }
            char* end = nullptr;
            errno = 0;
            long long n = std::strtoll(v.c_str(), &end, 10);
            if (errno != 0 || *end != '\0' || n < lo || n > hi)
            {
                return false;
            }
            out = n;
            return true;
        }

        bool is_sha1_hex(const std::string& h)
        {
            if (h.size() != 40)
            {
                return false;
            }
            for (char c : h)
            {
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                {
                    return false;
                }
            }
            return true;
        }

        // Numeric fields shared by several messages.
        bool get_size(const KeyValues& kv, const char* key, size_t& out, std::string& err)
        {
            const std::string* v = need(kv, key, err);
            long long n = 0;
            if (!v || !parse_int(*v, 0, std::numeric_limits<long long>::max(), n))
            {
                if (v) { err = std::string("bad ") + key + ": " + *v; }
                return false;
            }
            out = static_cast<size_t>(n);
            return true;
        }

        bool get_mtime(const KeyValues& kv, time_t& out, std::string& err)
        {
            const std::string* v = need(kv, "mtime", err);
            long long n = 0;
            if (!v || !parse_int(*v, std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max(), n))
            {
                if (v) { err = "bad mtime: " + *v; }
                return false;
            }
            out = static_cast<time_t>(n);
            return true;
        }

        bool get_permission(const KeyValues& kv, int& out, std::string& err)
        {
            const std::string* v = need(kv, "permission", err);
            long long n = 0;
            if (!v || !parse_int(*v, 0, 07777, n))
            {
                if (v) { err = "bad permission: " + *v; }
                return false;
            }
            out = static_cast<int>(n);
            return true;
        }

        // Parses status=complete|fail.
        bool get_status(const KeyValues& kv, bool& ok, std::string& err)
        {
            const std::string* v = need(kv, "status", err);
            if (!v)
            {
                return false;
            }
            if (*v != "complete" && *v != "fail")
            {
                err = "bad status: " + *v;
                return false;
            }
            ok = (*v == "complete");
            return true;
        }

        std::string optional(const KeyValues& kv, const char* key)
        {
            auto it = kv.find(key);
            return it == kv.end() ? std::string() : it->second;
        }

        // Writes the JS1.0 header followed by body into out.
        ExpectBool frame(const std::string& body, std::vector<uint8_t>& out)
        {
            if (body.size() > MAX_COMMAND_LEN)
            {
                return fail("command too long: " + std::to_string(body.size()), EMSGSIZE);
            }
            char header[32];
            int n = std::snprintf(header, sizeof header, "JS1.0%c%08zu%c", FIELD_SEP, body.size(), FIELD_SEP);
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
                 append_kv(body, "cmd", "DATA-SCAN"),
                 append_kv(body, "file", info.rpath),
                 append_kv(body, "size", std::to_string(info.size)),
                 append_kv(body, "mtime", std::to_string(static_cast<long long>(info.mtime))),
                 append_kv(body, "permission", std::to_string(info.permission)),
                 append_kv(body, "status", "continue"),
             })
        {
            if (!r)
            {
                return r;
            }
        }
        return frame(body, out);
    }

    ExpectBool convertToWire(const WriteRequest& req, std::vector<uint8_t>& out)
    {
        std::string body;
        for (auto r : {
                 append_kv(body, "cmd", "REQ-WRITE"),
                 append_kv(body, "directory", req.directory),
                 append_kv(body, "file", req.rpath),
                 append_kv(body, "hash", req.hash),
                 append_kv(body, "total_size", std::to_string(req.total_size)),
                 append_kv(body, "mtime", std::to_string(static_cast<long long>(req.mtime))),
                 append_kv(body, "permission", std::to_string(req.permission)),
             })
        {
            if (!r)
            {
                return r;
            }
        }
        return frame(body, out);
    }

    ExpectBool convertToWire(const ReadResponse& rsp, std::vector<uint8_t>& out)
    {
        std::string body;
        for (auto r : {
                 append_kv(body, "cmd", "RSP-READ"),
                 append_kv(body, "directory", rsp.directory),
                 append_kv(body, "file", rsp.rpath),
             })
        {
            if (!r)
            {
                return r;
            }
        }
        if (rsp.ok)
        {
            for (auto r : {
                     append_kv(body, "hash", rsp.hash),
                     append_kv(body, "mtime", std::to_string(static_cast<long long>(rsp.mtime))),
                     append_kv(body, "permission", std::to_string(rsp.permission)),
                 })
            {
                if (!r)
                {
                    return r;
                }
            }
        }
        if (auto r = append_kv(body, "status", rsp.ok ? "complete" : "fail"); !r)
        {
            return r;
        }
        if (!rsp.text.empty())
        {
            if (auto r = append_kv(body, "text", rsp.text); !r)
            {
                return r;
            }
        }
        return frame(body, out);
    }

    ExpectBool convertToWire(const ScanResponse& rsp, std::vector<uint8_t>& out)
    {
        std::string body;
        if (auto r = append_kv(body, "cmd", "RSP-SCAN"); !r)
        {
            return r;
        }
        if (auto r = append_kv(body, "status", rsp.ok ? "complete" : "fail"); !r)
        {
            return r;
        }
        if (!rsp.text.empty())
        {
            if (auto r = append_kv(body, "text", rsp.text); !r)
            {
                return r;
            }
        }
        return frame(body, out);
    }

    ExpectBool convertFromWire(const KeyValues& kv, ScanRequest& out)
    {
        std::string err;
        const std::string* dir = need(kv, "directory", err);
        if (!dir)
        {
            return fail("REQ-SCAN: " + err, EPROTO);
        }
        ScanRequest req;
        req.directory = *dir;
        std::string joined = optional(kv, "excludes");
        size_t start = 0;
        while (!joined.empty() && start <= joined.size())
        {
            size_t end = joined.find(EXCLUDE_SEP, start);
            if (end == std::string::npos)
            {
                end = joined.size();
            }
            req.excludes.push_back(joined.substr(start, end - start));
            start = end + 1;
        }
        out = std::move(req);
        return true;
    }

    ExpectBool convertFromWire(const KeyValues& kv, ScanInfo& out)
    {
        std::string err;
        ScanInfo info = out;
        const std::string* file = need(kv, "file", err);
        if (!file || !get_size(kv, "size", info.size, err) || !get_mtime(kv, info.mtime, err)
            || !get_permission(kv, info.permission, err))
        {
            return fail("DATA-SCAN: " + err, EPROTO);
        }
        info.rpath = *file;
        out = std::move(info);
        return true;
    }

    ExpectBool convertFromWire(const KeyValues& kv, ScanResponse& out)
    {
        std::string err;
        ScanResponse rsp;
        if (!get_status(kv, rsp.ok, err))
        {
            return fail("RSP-SCAN: " + err, EPROTO);
        }
        rsp.text = optional(kv, "text");
        out = std::move(rsp);
        return true;
    }

    ExpectBool convertFromWire(const KeyValues& kv, WriteRequest& out)
    {
        std::string err;
        WriteRequest req;
        const std::string* dir = need(kv, "directory", err);
        const std::string* file = dir ? need(kv, "file", err) : nullptr;
        const std::string* hash = file ? need(kv, "hash", err) : nullptr;
        if (!hash || !get_size(kv, "total_size", req.total_size, err) || !get_mtime(kv, req.mtime, err)
            || !get_permission(kv, req.permission, err))
        {
            return fail("REQ-WRITE: " + err, EPROTO);
        }
        if (!is_sha1_hex(*hash))
        {
            return fail("REQ-WRITE: bad hash: " + *hash, EPROTO);
        }
        req.directory = *dir;
        req.rpath = *file;
        req.hash = *hash;
        out = std::move(req);
        return true;
    }

    ExpectBool convertFromWire(const KeyValues& kv, ReadResponse& out)
    {
        std::string err;
        ReadResponse rsp;
        const std::string* dir = need(kv, "directory", err);
        const std::string* file = dir ? need(kv, "file", err) : nullptr;
        if (!file || !get_status(kv, rsp.ok, err))
        {
            return fail("RSP-READ: " + err, EPROTO);
        }
        rsp.directory = *dir;
        rsp.rpath = *file;
        rsp.text = optional(kv, "text");
        if (rsp.ok)
        {
            const std::string* hash = need(kv, "hash", err);
            if (!hash || !get_mtime(kv, rsp.mtime, err) || !get_permission(kv, rsp.permission, err))
            {
                return fail("RSP-READ: " + err, EPROTO);
            }
            if (!is_sha1_hex(*hash))
            {
                return fail("RSP-READ: bad hash: " + *hash, EPROTO);
            }
            rsp.hash = *hash;
        }
        out = std::move(rsp);
        return true;
    }
}
