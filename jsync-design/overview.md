# high level
jsync syncs files between two hosts. 
## features
- File syncing is bidirectional and handles deletes properly.
- SQLite database is used as the archive
- Files and directories do not have to reside in similar paths
	- hostA:/home/jeff/.bashrc=hostB:/backup/.bashrc
- Directories can be synced
	- hostA:/home/jeff/projects/reaper=hostB:/home/jrichards/projects/reaper
- Mirror mode
	- master-slave. The destination is always made identicle to the source
- Sync mode
	- Files are syncronized between the two hosts

## Architecture
### SQLite
There is one SQLite database to store file state. 
## Processes
There is a server process and client process that communicates with each other to scan files, transfer and update the database.
### Server
The server process runs on the host **without** the database. It is remotly spawned by the client when the client starts.
### Client
The client process manages the database and sends request/response messages to the server for file information. It also initiaizes any file transfers and updates the databse once the transfer completes and is verified.

## Components

| Component             | Responsibility                             |
| --------------------- | ------------------------------------------ |
| Archive               | Last successfully synchronized file states |
| Sync journal          | Pending and completed operations           |
| Transfer engine       | Copies files and verifies integrity        |
| Reconciliation engine | Recovers from interrupted operations       |
| SQLite transaction    | Commits the new archive state              |
|                       |                                            |
# Command line args
```
jsync create <path-to-database> <ssh-connect>
Create the SQLite database. No files are populated until the sync.
ssh-connect: The ssh user@host string for connection
```
```
jsync add <path-to-database> <local-path> <remote-path>
Add a file or directory for syncing.
local-path and remote-path are full pathnames and may be a file or a directory.
```
```
jsync sync <path-to-database> [--dry-run]
sync the files.
```

```
jsync status <path-to-database> [--changes]
   Status of the archive
   status of the journal
   num directories
   num files
   last sync date
if --changes is used:
	num files modified on remote
	num files modified locally
	num files created on the remote
	num files created locally
	num of conflicts
	num files deleted remotely
	num files deleted locally
```

```
jsync exclude <path-to-database> --show
--show: Display all excludes

jsync add-exclude <path-to-database> <local-path-to-exclude> <remote-path-to-exclude>
```

```
jsync server --port=<port>
Start the jsync server.
This is invoked by the 'sync' command
```