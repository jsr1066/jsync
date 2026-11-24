#include "scan.h"

#include <cerrno>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

#include "wire.h"

namespace jsync_ns
{
    namespace
    {
        std::unexpected<Error> fail(std::string message, int code)
        {
            return std::unexpected(Error{std::move(message), code, ErrorKind::Errno});
        }

        // Parses a decimal integer; false if v is empty or malformed.
        bool parse_int(const std::string& v, long long& out)
        {
            if (v.empty())
            {
                return false;
            }
            char* end = nullptr;
            errno = 0;
            out = std::strtoll(v.c_str(), &end, 10);
            return errno == 0 && *end == '\0';
        }

        bool differs(const ScanInfo& s, const FileRecord& r)
        {
            return static_cast<long long>(s.size) != r.size || static_cast<long long>(s.mtime) != r.mtime;
        }
    }

    Scan::Scan(Database& db, Peer& peer)
        : db_(db)
        , peer_(peer)
    {
    }

    ExpectActions Scan::plan()
    {
        auto items = db_.sync_items();
        if (!items)
        {
            return std::unexpected(items.error());
        }

        std::vector<Action> actions;
        for (const SyncItem& item : *items)
        {
            if (!item.directory)
            {
                continue;
            }
            auto part = plan_directory(item);
            if (!part)
            {
                return std::unexpected(part.error());
            }
            actions.insert(actions.end(), part->begin(), part->end());
        }
        return actions;
    }

    ExpectActions Scan::plan_directory(const SyncItem& item)
    {
        std::vector<ScanInfo> local;
        if (auto r = ops_.scan(item.client_path, item.excludes, local); !r)
        {
            return std::unexpected(r.error());
        }
        auto stored = db_.files_in(item.client_path);
        if (!stored)
        {
            return std::unexpected(stored.error());
        }
        std::vector<ScanInfo> server;
        if (auto r = scan_server(item, server); !r)
        {
            return std::unexpected(r.error());
        }
        return compare(item, local, server, *stored);
    }

    ExpectBool Scan::scan_server(const SyncItem& item, std::vector<ScanInfo>& results)
    {
        results.clear();
        std::vector<uint8_t> wire;
        if (auto r = convertToWire(ScanRequest{item.server_path, item.excludes}, wire); !r)
        {
            return std::unexpected(r.error());
        }
        if (auto r = peer_.send(wire.data(), wire.size()); !r)
        {
            return std::unexpected(r.error());
        }

        for (;;)
        {
            std::string command;
            std::map<std::string, std::string> kv;
            std::vector<uint8_t> chunk;
            if (auto r = read_command(peer_, command, kv, chunk); !r)
            {
                return std::unexpected(r.error());
            }

            if (command == "DATA-SCAN")
            {
                long long size = 0, mtime = 0, perm = 0;
                if (!kv.contains("file") || !parse_int(kv["size"], size) || !parse_int(kv["mtime"], mtime)
                    || !parse_int(kv["permission"], perm))
                {
                    return fail("malformed DATA-SCAN from server", EPROTO);
                }
                ScanInfo info;
                info.directory = item.server_path;
                info.rpath = kv["file"];
                info.permission = static_cast<int>(perm);
                info.size = static_cast<size_t>(size);
                info.mtime = static_cast<time_t>(mtime);
                results.push_back(std::move(info));
                continue;
            }
            if (command != "RSP-SCAN")
            {
                return fail("unexpected response to REQ-SCAN: " + command, EPROTO);
            }
            if (kv["status"] != "complete")
            {
                return fail("server scan of " + item.server_path + " failed: " + kv["text"], EIO);
            }
            return true;
        }
    }

    std::vector<Action> Scan::compare(const SyncItem& item,
                                      const std::vector<ScanInfo>& local,
                                      const std::vector<ScanInfo>& server,
                                      const std::vector<FileRecord>& stored)
    {
        std::map<std::string, const ScanInfo*> l, s;
        std::map<std::string, const FileRecord*> d;
        std::set<std::string> names; // ordered, so the actions are ordered by rpath
        for (const ScanInfo& i : local)
        {
            l[i.rpath] = &i;
            names.insert(i.rpath);
        }
        for (const ScanInfo& i : server)
        {
            s[i.rpath] = &i;
            names.insert(i.rpath);
        }
        for (const FileRecord& r : stored)
        {
            d[r.rpath] = &r;
            names.insert(r.rpath);
        }

        std::vector<Action> actions;
        auto add = [&](ActionType type, const std::string& rpath)
        {
            actions.push_back(Action{type, item.client_path, item.server_path, rpath});
        };

        for (const std::string& name : names)
        {
            auto li = l.find(name);
            auto si = s.find(name);
            auto di = d.find(name);
            const ScanInfo* lf = li == l.end() ? nullptr : li->second;
            const ScanInfo* sf = si == s.end() ? nullptr : si->second;
            const FileRecord* df = di == d.end() ? nullptr : di->second;

            if (lf && sf)
            {
                if (lf->size == sf->size && lf->mtime == sf->mtime)
                {
                    continue; // already identical
                }
                if (!df)
                {
                    add(ActionType::Conflict, name); // both new, different
                    continue;
                }
                bool lc = differs(*lf, *df);
                bool sc = differs(*sf, *df);
                if (lc && sc)
                {
                    add(ActionType::Conflict, name);
                }
                else if (lc)
                {
                    add(ActionType::WriteToServer, name);
                }
                else if (sc)
                {
                    add(ActionType::ReadFromServer, name);
                }
                // else both match the record but not each other (e.g. a past
                // transfer did not preserve mtime): leave alone.
            }
            else if (lf)
            {
                if (!df)
                {
                    add(ActionType::WriteToServer, name);
                }
                else // deleted on the server
                {
                    add(differs(*lf, *df) ? ActionType::Conflict : ActionType::DeleteLocal, name);
                }
            }
            else if (sf)
            {
                if (!df)
                {
                    add(ActionType::ReadFromServer, name);
                }
                else // deleted locally
                {
                    add(differs(*sf, *df) ? ActionType::Conflict : ActionType::DeleteServer, name);
                }
            }
            // else: gone from both sides, nothing to do
        }
        return actions;
    }
}
