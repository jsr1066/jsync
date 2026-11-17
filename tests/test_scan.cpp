#include "commands.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

static void write_file(const fs::path& p, const std::string& content)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
}

static std::vector<std::string> rpaths(const std::vector<ScanInfo>& r)
{
    std::vector<std::string> out;
    for (const auto& s : r)
    {
        out.push_back(s.rpath);
    }
    return out;
}

int main()
{
    fs::path root = fs::temp_directory_path() / ("jsync-test-scan-" + std::to_string(::getpid()));
    fs::remove_all(root);
    write_file(root / "a.txt", "hello\n");
    write_file(root / "b.o", "obj");
    write_file(root / "sub/c.txt", "c");
    write_file(root / "sub/deep/d.txt", "dd");
    write_file(root / "build/x/z.txt", "z");
    fs::permissions(root / "a.txt", fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    fs::create_symlink("a.txt", root / "link");

    Commands cmd;
    std::vector<ScanInfo> results;

    // No excludes: all regular files, sorted, symlink skipped.
    {
        auto res = cmd.scan(root.string(), {}, results);
        CHECK(res.has_value());
        std::vector<std::string> expected = {"a.txt", "b.o", "build/x/z.txt", "sub/c.txt", "sub/deep/d.txt"};
        CHECK(rpaths(results) == expected);
    }

    // Metadata matches stat().
    {
        cmd.scan(root.string(), {}, results);
        struct stat st;
        CHECK(::stat((root / "a.txt").c_str(), &st) == 0);
        CHECK(!results.empty() && results[0].rpath == "a.txt");
        if (!results.empty())
        {
            CHECK(results[0].size == 6);
            CHECK(results[0].permission == 0640);
            CHECK(results[0].mtime == st.st_mtime);
            CHECK(results[0].directory == root.string());
        }
    }

    // Excludes match on rpath; matching directory prunes its subtree.
    {
        auto res = cmd.scan(root.string(), {"\\.o$", "^build"}, results);
        CHECK(res.has_value());
        std::vector<std::string> expected = {"a.txt", "sub/c.txt", "sub/deep/d.txt"};
        CHECK(rpaths(results) == expected);
    }

    // Excluding a nested directory by path.
    {
        cmd.scan(root.string(), {"^sub/deep"}, results);
        std::vector<std::string> expected = {"a.txt", "b.o", "build/x/z.txt", "sub/c.txt"};
        CHECK(rpaths(results) == expected);
    }

    // Invalid regex returns an error.
    {
        auto res = cmd.scan(root.string(), {"("}, results);
        CHECK(!res.has_value());
    }

    // Missing directory returns an error.
    {
        auto res = cmd.scan((root / "nope").string(), {}, results);
        CHECK(!res.has_value());
        if (!res.has_value())
        {
            CHECK(res.error().ecode != 0);
        }
    }

    // A file is not a directory.
    {
        auto res = cmd.scan((root / "a.txt").string(), {}, results);
        CHECK(!res.has_value());
    }

    fs::remove_all(root);
    if (failures == 0)
    {
        std::cout << "all scan tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
