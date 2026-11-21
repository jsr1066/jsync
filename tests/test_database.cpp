#include "database.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

#include <sqlite3.h>

namespace fs = std::filesystem;
using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

int main()
{
    fs::path root = fs::temp_directory_path() / ("jsync-test-database-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "dir");
    std::ofstream(root / "file.txt") << "hello";
    fs::create_symlink(root / "dir", root / "dirlink");

    std::string db_path = (root / "test.db").string();
    std::string file = (root / "file.txt").string();
    std::string dir = (root / "dir").string();

    Database db;

    // add() on a closed database fails.
    CHECK(!db.add(file, "/srv/file.txt"));

    CHECK(db.create(db_path, "user@host"));

    CHECK(db.add(file, "/srv/file.txt"));
    CHECK(db.add(dir, "/srv/dir"));
    CHECK(db.add((root / "dirlink").string(), "/srv/dirlink"));

    // Missing path.
    auto missing = db.add((root / "nope").string(), "/srv/nope");
    CHECK(!missing);
    if (!missing)
    {
        CHECK(missing.error().kind == ErrorKind::Errno);
        CHECK(missing.error().ecode == ENOENT);
    }

    // Duplicate client_path.
    auto dup = db.add(file, "/srv/other");
    CHECK(!dup);
    if (!dup)
        CHECK(dup.error().kind == ErrorKind::Sqlite);

    auto items = db.sync_items();
    CHECK(items.has_value());
    if (items)
    {
        // Ordered by client_path: dir, dirlink, file.txt.
        CHECK(items->size() == 3);
        if (items->size() == 3)
        {
            CHECK((*items)[0].client_path == dir);
            CHECK((*items)[0].server_path == "/srv/dir");
            CHECK((*items)[0].directory);
            CHECK((*items)[1].client_path == (root / "dirlink").string());
            CHECK((*items)[1].directory);
            CHECK((*items)[2].client_path == file);
            CHECK((*items)[2].server_path == "/srv/file.txt");
            CHECK(!(*items)[2].directory);
        }
    }

    // Nothing is added to files by add().
    auto none = db.files_in(dir);
    CHECK(none.has_value() && none->empty());

    db.close();
    CHECK(!db.sync_items());
    CHECK(!db.files_in(dir));

    // Empty before anything is written.
    CHECK(db.open(db_path));
    CHECK(db.put_files("/a", {}));
    auto empty = db.files_in("/a");
    CHECK(empty.has_value() && empty->empty());

    CHECK(db.put_files("/a", {{"z.txt", "h1", 10, 100, 420}, {"sub/b.txt", "h2", 20, 200, 384}}));
    CHECK(db.put_files("/b", {{"other.txt", "", 0, 0, 0}}));

    auto recs = db.files_in("/a");
    CHECK(recs.has_value());
    if (recs && recs->size() == 2)
    {
        CHECK((*recs)[0].rpath == "sub/b.txt");
        CHECK((*recs)[0].hash == "h2");
        CHECK((*recs)[0].size == 20);
        CHECK((*recs)[0].mtime == 200);
        CHECK((*recs)[0].permissions == 384);
        CHECK((*recs)[1].rpath == "z.txt");
        CHECK((*recs)[1].hash == "h1");
    }
    else
        CHECK(false);

    // Writing again replaces matching rpaths, adds new ones, keeps the rest.
    CHECK(db.put_files("/a", {{"z.txt", "h1b", 11, 101, 420}, {"new.txt", "h3", 1, 2, 3}}));
    recs = db.files_in("/a");
    CHECK(recs.has_value() && recs->size() == 3);
    if (recs && recs->size() == 3)
    {
        CHECK((*recs)[0].rpath == "new.txt");
        CHECK((*recs)[1].rpath == "sub/b.txt");
        CHECK((*recs)[1].hash == "h2");
        CHECK((*recs)[2].rpath == "z.txt");
        CHECK((*recs)[2].hash == "h1b");
        CHECK((*recs)[2].size == 11);
    }

    // A NULL hash reads back as empty; other directories are not mixed in.
    auto other = db.files_in("/b");
    CHECK(other.has_value() && other->size() == 1);
    if (other && other->size() == 1)
        CHECK((*other)[0].hash.empty());
    auto unknown = db.files_in("/nope");
    CHECK(unknown.has_value() && unknown->empty());
    // Excludes.
    fs::create_directories(root / "dir2");
    std::string dir2 = (root / "dir2").string();
    CHECK(db.add(dir2, "/srv/dir2", {"\\.log$", "^build$"}));

    auto bad = db.add((root / "dir").string() + "/", "/srv/x", {"("});
    CHECK(!bad);
    if (!bad)
        CHECK(bad.error().kind == ErrorKind::Regex);
    CHECK(!db.add(file, "/srv/x", {"a"}));
    CHECK(!db.add(dir2, "/srv/x", {"a\x1f" "b"}));

    auto sync = db.sync_items();
    CHECK(sync.has_value());
    if (sync)
    {
        for (const auto& item : *sync)
        {
            if (item.client_path == dir2)
            {
                CHECK(item.excludes.size() == 2);
                if (item.excludes.size() == 2)
                {
                    CHECK(item.excludes[0] == "\\.log$");
                    CHECK(item.excludes[1] == "^build$");
                }
            }
            else
                CHECK(item.excludes.empty());
        }
    }

    // set_excludes deletes the files rows the new excludes match, including
    // files under an excluded directory, and keeps the rest.
    CHECK(db.put_files(dir2, {{"a.log", "h", 1, 1, 1}, {"keep.txt", "h", 1, 1, 1},
                              {"build/out.o", "h", 1, 1, 1}, {"build2/x.txt", "h", 1, 1, 1}}));
    CHECK(db.set_excludes(dir2, {"\\.log$", "^build$"}));
    auto kept = db.files_in(dir2);
    CHECK(kept.has_value() && kept->size() == 2);
    if (kept && kept->size() == 2)
    {
        CHECK((*kept)[0].rpath == "build2/x.txt");
        CHECK((*kept)[1].rpath == "keep.txt");
    }

    // An invalid regex changes nothing.
    CHECK(!db.set_excludes(dir2, {"("}));
    sync = db.sync_items();
    if (sync)
        for (const auto& item : *sync)
            if (item.client_path == dir2)
                CHECK(item.excludes.size() == 2);

    // Clearing the excludes stores none; files rows are untouched.
    CHECK(db.set_excludes(dir2, {}));
    sync = db.sync_items();
    if (sync)
        for (const auto& item : *sync)
            if (item.client_path == dir2)
                CHECK(item.excludes.empty());
    kept = db.files_in(dir2);
    CHECK(kept.has_value() && kept->size() == 2);

    // Only a directory in the sync table can have excludes.
    CHECK(!db.set_excludes(file, {"a"}));
    auto notfound = db.set_excludes("/nope", {"a"});
    CHECK(!notfound);
    if (!notfound)
        CHECK(notfound.error().ecode == ENOENT);

    db.close();
    CHECK(!db.set_excludes(dir2, {}));
    CHECK(!db.put_files("/a", {{"x", "h", 1, 1, 1}}));

    fs::remove_all(root);
    if (failures == 0)
        std::cout << "test_database: all passed\n";
    return failures == 0 ? 0 : 1;
}
