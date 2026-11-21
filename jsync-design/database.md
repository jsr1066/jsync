# Schema
## Settings
Database-wide key/value settings.
- ssh_connect: the ssh user@host string of the remote, set by `jsync create`. It does not change.
```
CREATE TABLE settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

## File metadata
Stores the metadata about each file or directory to be synchronized.
The directory field is a boolean indicating if the client_path is a single file or
a directory that needs to be scanned
```
CREATE TABLE files (
    client_path TEXT PRIMARY KEY,
    server_path TEXT,
    hash TEXT,
    size INTEGER,
    mtime INTEGER,
    permissions INTEGER,
    directory INTEGER
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