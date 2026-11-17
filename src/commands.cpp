#include "commands.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>

#include "sha1.h"

namespace fs = std::filesystem;

namespace jsync_ns
{
    namespace
    {
        // Joins directory and rpath, rejecting empty, absolute and ".." paths so a
        // remote client cannot reach outside directory.
        std::expected<fs::path, Error> resolve_path(const std::string& directory, const std::string& rpath)
        {
            const fs::path rel(rpath);
            if (rel.empty() || rel.is_absolute()
                || std::any_of(rel.begin(), rel.end(), [](const fs::path& part) { return part == ".."; }))
            {
                return std::unexpected(Error{"invalid relative path: " + rpath, EINVAL});
            }
            return fs::path(directory) / rel;
        }
    }

    ExpectBool Commands::scan(const std::string& directory, const std::vector<std::string>& excludes, std::vector<ScanInfo>& results)
    {
        results.clear();

        std::vector<std::regex> patterns;
        patterns.reserve(excludes.size());
        for (const auto& exclude : excludes)
        {
            try
            {
                patterns.emplace_back(exclude);
            }
            catch (const std::regex_error& e)
            {
                return std::unexpected(Error{"invalid exclude regex '" + exclude + "': " + e.what(), static_cast<int>(e.code())});
            }
        }

        std::error_code ec;
        const fs::path root(directory);
        if (!fs::is_directory(root, ec))
        {
            int code = ec ? ec.value() : ENOTDIR;
            return std::unexpected(Error{"not a directory: " + directory, code});
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return std::unexpected(Error{"cannot read directory " + directory + ": " + ec.message(), ec.value()});
        }

        const fs::recursive_directory_iterator end;
        while (it != end)
        {
            const fs::directory_entry& entry = *it;
            const std::string rpath = entry.path().lexically_relative(root).generic_string();

            struct stat st;
            bool have_stat = ::lstat(entry.path().c_str(), &st) == 0;

            bool excluded = std::any_of(patterns.begin(), patterns.end(),
                [&rpath](const std::regex& re) { return std::regex_search(rpath, re); });

            if (excluded)
            {
                if (have_stat && S_ISDIR(st.st_mode))
                {
                    it.disable_recursion_pending();
                }
            }
            else if (have_stat && S_ISREG(st.st_mode))
            {
                ScanInfo info;
                info.directory = directory;
                info.rpath = rpath;
                info.permission = static_cast<int>(st.st_mode & 07777);
                info.size = static_cast<size_t>(st.st_size);
                info.mtime = st.st_mtime;
                results.push_back(std::move(info));
            }

            it.increment(ec);
            if (ec)
            {
                return std::unexpected(Error{"error scanning " + directory + ": " + ec.message(), ec.value()});
            }
        }

        std::sort(results.begin(), results.end(),
            [](const ScanInfo& a, const ScanInfo& b) { return a.rpath < b.rpath; });

        return true;
    }

    ExpectBool Commands::hash(const std::string& directory, const std::string& rpath, std::string& hash)
    {
        hash.clear();

        auto resolved = resolve_path(directory, rpath);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        const fs::path& full = *resolved;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0)
        {
            int code = errno;
            return std::unexpected(Error{"cannot stat " + full.string() + ": " + std::strerror(code), code});
        }
        if (!S_ISREG(st.st_mode))
        {
            return std::unexpected(Error{"not a regular file: " + full.string(), EINVAL});
        }

        std::ifstream in(full, std::ios::binary);
        if (!in)
        {
            int code = errno ? errno : EIO;
            return std::unexpected(Error{"cannot open " + full.string() + ": " + std::strerror(code), code});
        }

        Sha1 sha;
        std::vector<char> buf(64 * 1024);
        while (in)
        {
            in.read(buf.data(), buf.size());
            sha.update(buf.data(), static_cast<size_t>(in.gcount()));
        }
        if (in.bad())
        {
            return std::unexpected(Error{"read error on " + full.string(), EIO});
        }

        hash = sha.hex_digest();
        return true;
    }

    ExpectReadContext Commands::open(const std::string& directory, const std::string& rpath)
    {
        auto resolved = resolve_path(directory, rpath);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        const fs::path& full = *resolved;

        ReadContext context;
        context.fd = ::open(full.c_str(), O_RDONLY | O_CLOEXEC);
        if (context.fd < 0)
        {
            int code = errno;
            return std::unexpected(Error{"cannot open " + full.string() + ": " + std::strerror(code), code});
        }

        struct stat st;
        if (::fstat(context.fd, &st) != 0)
        {
            int code = errno;
            return std::unexpected(Error{"cannot stat " + full.string() + ": " + std::strerror(code), code});
        }
        if (!S_ISREG(st.st_mode))
        {
            return std::unexpected(Error{"not a regular file: " + full.string(), EINVAL});
        }

        context.directory = directory;
        context.rpath = rpath;
        context.total_size = static_cast<size_t>(st.st_size);
        return std::move(context);
    }

    ExpectBool Commands::read(const ReadContext& context, Chunk& chunk)
    {
        chunk.directory = context.directory;
        chunk.rpath = context.rpath;
        chunk.total_size = context.total_size;
        chunk.chunk_size = 0;

        if (context.fd < 0)
        {
            return std::unexpected(Error{"read on closed context", EBADF});
        }

        // Fill the whole chunk unless EOF intervenes; read(2) may return short counts.
        size_t filled = 0;
        while (filled < MAX_CHUNK)
        {
            ssize_t n = ::read(context.fd, chunk.bytes + filled, MAX_CHUNK - filled);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                int code = errno;
                return std::unexpected(Error{"read error on " + context.rpath + ": " + std::strerror(code), code});
            }
            if (n == 0)
            {
                break;
            }
            filled += static_cast<size_t>(n);
        }

        chunk.chunk_size = filled;
        return filled > 0;
    }
}
