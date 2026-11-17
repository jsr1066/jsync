#include "commands.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <regex>

#include <sys/stat.h>

namespace fs = std::filesystem;

namespace jsync_ns
{
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
}
