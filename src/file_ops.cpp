#include "file_ops.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <spdlog/spdlog.h>

#include "sha1.h"

namespace fs = std::filesystem;

namespace jsync_ns
{
    namespace
    {
        // Logs the error for the named command and wraps it for return.
        std::unexpected<Error> fail(const char* command, Error error)
        {
            spdlog::error("{}: {} (code {})", command, error.err, error.ecode);
            return std::unexpected(std::move(error));
        }

        // The temp file written for rpath: same directory, so rename() stays on
        // one filesystem.
        fs::path temp_path_for(const fs::path& full)
        {
            return full.parent_path() / ("." + full.filename().string() + ".jsync.tmp");
        }

        // Joins directory and rpath, rejecting empty, absolute and ".." paths so a
        // remote client cannot reach outside directory.
        std::expected<fs::path, Error> resolve_path(const std::string& directory, const std::string& rpath)
        {
            const fs::path rel(rpath);
            if (rel.empty() || rel.is_absolute()
                || std::any_of(rel.begin(), rel.end(), [](const fs::path& part) { return part == ".."; }))
            {
                return std::unexpected(Error{"invalid relative path: " + rpath, EINVAL, ErrorKind::Errno});
            }
            return fs::path(directory) / rel;
        }
    }

    ExpectBool FileOps::scan(const std::string& directory, const std::vector<std::string>& excludes, std::vector<ScanInfo>& results)
    {
        spdlog::info("scan: directory={} excludes={}", directory, excludes.size());
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
                return fail("scan", Error{"invalid exclude regex '" + exclude + "': " + e.what(), static_cast<int>(e.code()), ErrorKind::Regex});
            }
        }

        std::error_code ec;
        const fs::path root(directory);
        if (!fs::is_directory(root, ec))
        {
            int code = ec ? ec.value() : ENOTDIR;
            return fail("scan", Error{"not a directory: " + directory, code, ErrorKind::Errno});
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return fail("scan", Error{"cannot read directory " + directory + ": " + ec.message(), ec.value(), ErrorKind::Errno});
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
                return fail("scan", Error{"error scanning " + directory + ": " + ec.message(), ec.value(), ErrorKind::Errno});
            }
        }

        std::sort(results.begin(), results.end(),
            [](const ScanInfo& a, const ScanInfo& b) { return a.rpath < b.rpath; });

        return true;
    }

    ExpectBool FileOps::hash(const std::string& directory, const std::string& rpath, std::string& hash)
    {
        spdlog::info("hash: directory={} rpath={}", directory, rpath);
        hash.clear();

        auto resolved = resolve_path(directory, rpath);
        if (!resolved)
        {
            return fail("hash", resolved.error());
        }
        const fs::path& full = *resolved;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0)
        {
            int code = errno;
            return fail("hash", Error{"cannot stat " + full.string() + ": " + std::strerror(code), code, ErrorKind::Errno});
        }
        if (!S_ISREG(st.st_mode))
        {
            return fail("hash", Error{"not a regular file: " + full.string(), EINVAL, ErrorKind::Errno});
        }

        std::ifstream in(full, std::ios::binary);
        if (!in)
        {
            int code = errno ? errno : EIO;
            return fail("hash", Error{"cannot open " + full.string() + ": " + std::strerror(code), code, ErrorKind::Errno});
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
            return fail("hash", Error{"read error on " + full.string(), EIO, ErrorKind::Errno});
        }

        hash = sha.hex_digest();
        return true;
    }

    ExpectIOContext FileOps::ropen(const std::string& directory, const std::string& rpath)
    {
        spdlog::info("ropen: directory={} rpath={}", directory, rpath);
        auto resolved = resolve_path(directory, rpath);
        if (!resolved)
        {
            return fail("ropen", resolved.error());
        }
        const fs::path& full = *resolved;

        IOContext context;
        context.fd = ::open(full.c_str(), O_RDONLY | O_CLOEXEC);
        if (context.fd < 0)
        {
            int code = errno;
            return fail("ropen", Error{"cannot open " + full.string() + ": " + std::strerror(code), code, ErrorKind::Errno});
        }

        struct stat st;
        if (::fstat(context.fd, &st) != 0)
        {
            int code = errno;
            return fail("ropen", Error{"cannot stat " + full.string() + ": " + std::strerror(code), code, ErrorKind::Errno});
        }
        if (!S_ISREG(st.st_mode))
        {
            return fail("ropen", Error{"not a regular file: " + full.string(), EINVAL, ErrorKind::Errno});
        }

        context.directory = directory;
        context.rpath = rpath;
        context.total_size = static_cast<size_t>(st.st_size);
        context.mtime = st.st_mtime;
        return context;
    }

    ExpectBool FileOps::read(IOContext& context, Chunk& chunk)
    {
        spdlog::debug("read: directory={} rpath={}", context.directory, context.rpath);
        chunk.directory = context.directory;
        chunk.rpath = context.rpath;
        chunk.total_size = context.total_size;
        chunk.mtime = context.mtime;
        chunk.chunk_size = 0;
        chunk.hash.clear();

        if (context.fd < 0)
        {
            return fail("read", Error{"read on closed context", EBADF, ErrorKind::Errno});
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
                return fail("read", Error{"read error on " + context.rpath + ": " + std::strerror(code), code, ErrorKind::Errno});
            }
            if (n == 0)
            {
                break;
            }
            filled += static_cast<size_t>(n);
        }

        chunk.chunk_size = filled;
        context.sha.update(chunk.bytes, filled);
        context.bytes_done += filled;
        if (filled == 0 || context.bytes_done >= context.total_size)
        {
            // hex_digest() finalizes, so use a copy in case read() is called again.
            Sha1 done = context.sha;
            chunk.hash = done.hex_digest();
        }
        return filled > 0;
    }

    ExpectIOContext FileOps::wopen(const std::string& directory, const std::string& rpath,
                                    size_t total_size, const std::string& expected_hash, time_t mtime)
    {
        spdlog::info("wopen: directory={} rpath={} total_size={} mtime={}", directory, rpath, total_size, static_cast<long long>(mtime));

        auto resolved = resolve_path(directory, rpath);
        if (!resolved)
        {
            return fail("wopen", resolved.error());
        }
        const fs::path temp = temp_path_for(*resolved);

        std::error_code ec;
        fs::create_directories(temp.parent_path(), ec);
        if (ec)
        {
            return fail("wopen", Error{"cannot create directory " + temp.parent_path().string() + ": " + ec.message(), ec.value(), ErrorKind::Errno});
        }

        IOContext context;
        context.fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (context.fd < 0)
        {
            int code = errno;
            return fail("wopen", Error{"cannot open " + temp.string() + ": " + std::strerror(code), code, ErrorKind::Errno});
        }

        context.directory = directory;
        context.rpath = rpath;
        context.total_size = total_size;
        context.mtime = mtime;
        context.expected_hash = expected_hash;
        context.temp_path = temp.string();
        return context;
    }

    ExpectBool FileOps::write(IOContext& context, const Chunk& chunk)
    {
        spdlog::debug("write: directory={} rpath={} chunk={}", context.directory, context.rpath, chunk.chunk_size);

        if (context.fd < 0)
        {
            return fail("write", Error{"write on closed context", EBADF, ErrorKind::Errno});
        }
        if (chunk.chunk_size > MAX_CHUNK
            || chunk.chunk_size > context.total_size - context.bytes_done)
        {
            return fail("write", Error{"too much data for " + context.rpath, EFBIG, ErrorKind::Errno});
        }

        size_t written = 0;
        while (written < chunk.chunk_size)
        {
            ssize_t n = ::write(context.fd, chunk.bytes + written, chunk.chunk_size - written);
            if (n < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                int code = errno;
                return fail("write", Error{"write error on " + context.temp_path + ": " + std::strerror(code), code, ErrorKind::Errno});
            }
            written += static_cast<size_t>(n);
        }
        context.sha.update(chunk.bytes, chunk.chunk_size);
        context.bytes_done += chunk.chunk_size;

        if (context.bytes_done < context.total_size)
        {
            return true;
        }

        // Complete: verify before the caller may rename the temp file into place.
        const std::string actual = context.sha.hex_digest();
        if (actual != context.expected_hash)
        {
            ::close(context.fd);
            context.fd = -1;
            ::unlink(context.temp_path.c_str());
            return fail("write", Error{"hash mismatch on " + context.rpath + ": expected " + context.expected_hash + ", got " + actual, EBADMSG, ErrorKind::Errno});
        }
        // Set the source's mtime on the temp file; rename(2) preserves it. This
        // must come after the last write, which would otherwise bump it.
        struct timeval times[2] = {{context.mtime, 0}, {context.mtime, 0}};
        if (::futimes(context.fd, times) != 0)
        {
            int code = errno;
            ::close(context.fd);
            context.fd = -1;
            ::unlink(context.temp_path.c_str());
            return fail("write", Error{"cannot set mtime on " + context.temp_path + ": " + std::strerror(code), code, ErrorKind::Errno});
        }
        if (::fsync(context.fd) != 0 || ::close(context.fd) != 0)
        {
            int code = errno;
            context.fd = -1;
            return fail("write", Error{"cannot flush " + context.temp_path + ": " + std::strerror(code), code, ErrorKind::Errno});
        }
        context.fd = -1;
        return false;
    }

    ExpectBool FileOps::rename(const std::string& directory, const std::string& rpath)
    {
        spdlog::info("rename: directory={} rpath={}", directory, rpath);

        auto resolved = resolve_path(directory, rpath);
        if (!resolved)
        {
            return fail("rename", resolved.error());
        }
        const fs::path& full = *resolved;
        const fs::path temp = temp_path_for(full);

        // rename(2) atomically replaces an existing file; the temp file is in the
        // same directory so this never crosses filesystems.
        if (::rename(temp.c_str(), full.c_str()) != 0)
        {
            int code = errno;
            return fail("rename", Error{"cannot rename " + temp.string() + " to " + full.string() + ": " + std::strerror(code), code, ErrorKind::Errno});
        }
        return true;
    }
}
