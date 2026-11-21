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

// Inserts rows into the files table directly; there is no API for it yet.
static void seed_files(const std::string& db_path)
{
    sqlite3* db = nullptr;
    sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
    sqlite3_exec(db,
        "INSERT INTO files VALUES ('/a', 'z.txt', 'h1', 10, 100, 420);"
        "INSERT INTO files VALUES ('/a', 'sub/b.txt', 'h2', 20, 200, 384);"
        "INSERT INTO files VALUES ('/b', 'other.txt', NULL, NULL, NULL, NULL);",
        nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

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

    seed_files(db_path);
    CHECK(db.open(db_path));

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

    // A NULL hash reads back as empty; other directories are not mixed in.
    auto other = db.files_in("/b");
    CHECK(other.has_value() && other->size() == 1);
    if (other && other->size() == 1)
        CHECK((*other)[0].hash.empty());
    auto unknown = db.files_in("/nope");
    CHECK(unknown.has_value() && unknown->empty());
    db.close();

    fs::remove_all(root);
    if (failures == 0)
        std::cout << "test_database: all passed\n";
    return failures == 0 ? 0 : 1;
}
