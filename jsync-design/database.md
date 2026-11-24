# Schema
## Settings
Database-wide key/value settings.
- ssh_connect: the ssh user@host string of the remote, set by `jsync create`. It does not change.
- last_sync_time: The time of the last sync. File mtimes are compared to this.
```
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

## Sync
The files and directories to be synchronized, added by `jsync add`.
client_path is the local path and server_path the remote path. directory is a boolean:
if true the paths are directories to be scanned (locally and remotely), otherwise
they are a single file to be synchronized.

excludes holds the regexes of files to skip in a directory, joined by the 0x1f
separator (the same format as the excludes value of REQ-SCAN). The same regexes are
used for the local and the remote scan and are matched against the rpath. It is NULL
when there are none, and always NULL for a single file.
When the excludes of a directory change, the files rows that match are deleted so that
they are not mistaken for deleted files.
```
CREATE TABLE sync (
    client_path TEXT PRIMARY KEY,
    server_path TEXT NOT NULL,
    directory INTEGER NOT NULL,
    excludes TEXT
);
```

## File metadata
The metadata of each file in a synchronized directory, as of the last successful sync.
client_path is the directory (a sync.client_path with directory true) and rpath is the
file's path relative to it, which can be used directly in a REQ-SCAN message.
All the files of a directory are found by querying on client_path.
```
CREATE TABLE files (
    client_path TEXT NOT NULL,
    rpath TEXT NOT NULL,
    hash TEXT,
    size INTEGER,
    mtime INTEGER,
    permissions INTEGER,
    PRIMARY KEY (client_path, rpath)
);
```

## Pending journal
The pending journal records pending operations in case of crashes or errors.
Entries are removed once the file has been transferred and the hash verified and ethe rename as succeded.
direction is 'push' (client to server) or 'pull' (server to client).

```
CREATE TABLE pending_journal (
    id INTEGER PRIMARY KEY,
    client_path TEXT NOT NULL,
    direction TEXT NOT NULL CHECK (direction IN ('push', 'pull')),
    expected_hash TEXT,
    status TEXT NOT NULL,
    created_at INTEGER
);
```
