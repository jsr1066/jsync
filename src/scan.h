#pragma once

#include <vector>

#include "database.h"
#include "file_ops.h"
#include "peer.h"
#include "structs.h"

namespace jsync_ns
{
    // Plans a sync: compares the local files, the server files and the state
    // stored in the database at the last sync, and decides what to do about
    // each file. Nothing is modified; executing the actions is up to the caller.
    class Scan
    {
    public:
        // db must be open and peer connected; both must outlive the Scan.
        Scan(Database& db, Peer& peer);

        // Plans every directory in the sync table (single-file items are not
        // handled yet and are skipped). The actions are ordered by client_path,
        // then rpath.
        ExpectActions plan();

    public: // public for unit tests
        // Pure decision function for one directory. A file has changed on a
        // side if its size or mtime differs from the stored record. See the
        // table in jsync-design/scan.md.
        static std::vector<Action> compare(const SyncItem& item,
                                           const std::vector<ScanInfo>& local,
                                           const std::vector<ScanInfo>& server,
                                           const std::vector<FileRecord>& stored);

    private:
        ExpectActions plan_directory(const SyncItem& item);

        // Sends REQ-SCAN for item and collects the DATA-SCAN entries up to RSP-SCAN.
        ExpectBool scan_server(const SyncItem& item, std::vector<ScanInfo>& results);

        Database& db_;
        Peer& peer_;
        FileOps ops_;
    };
}
