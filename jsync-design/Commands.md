# Wire format
This describes the wire protocol for each command. The protocol is an ASCII based key=value pairs on a single line. Fields are delimited by the byte 0x10 (written `<0x10>` below). Since 0x10 is a control character it cannot appear in a file name, so values never need escaping. 

The commands that take a directory=\<...\> and file=\<relative-path\> are in the remote server file paths. It is the responsibilty of the client to map into local paths.
## Client and server
In this spec, the client is the host that posseses the SQLite database. It is responsible for remotely starting the server through the *jsync --server* command. All commands are sent from the client to the server. It is the client that makes all decisions about what files get transmitted and in what direction as well as determining which file should be deleted.

# Fields
The value are all transmitted in ASCII, no binary except for the file chunks.
## Header
The format of the header is as follows
JS\<version\>\<0x10\>      This is the introduction. The version will be 1.0. The length of this field is always 5 bytes
\<length\>\<0x10\>           The length in bytes of the remaining data. This length does not include any chunk data. The length of this field is always 8 bytes.

The length of the header is always 15 bytes including delimiters.

Since the JS and length are fixed size, a read of 15 bytes will always retrive the JS\<version\>\<0x10\>\<length\>\<0x10\>
The JS tag can be used to check alignment and the length will indicate the next read length to get all the kv pairs.
## Body
key=value\<0x10\>...        The key value pairs, these can be considered arguments to the command
# KV Fields
Listing of all defined keys

| fieldname  | value                 | Note                                 |
| ---------- | --------------------- | ------------------------------------ |
| cmd        | string                | The command                          |
| directory  | string                | Specifies the server side directory  |
| file       | string                | The relative path to the directory   |
| exclude    | regex\<0x1f\>...                 | Pattern of files to exclude<br>One exclude key with multiple regexes          |
| hash       | hex                   | File hash                            |
| total_size | size_t                | Size of file                         |
| chunk_size | size_t                | Size of file data following the line |
| status     | "continue"<br>"complete"<br>"fail" | Completion status<br>                |
| text       | string                | Error message                        |
| mtime      | int                   | time_t value for modification time   |
| permission | int                   | file permission                      |

# Commands
| Command | description               |
| ------- | ------------------------- |
| SCAN    | File listing and metadata |
| META    | Get file metadata         |
| READ    | Read file data            |
| WRITE   | Write file data           |
| RENAME  | Rename temporary file     |
| DELETE  | Delete file               |
| QUIT    | Terminate process         |

# SCAN
The scan request is only ever sent from client to server
## request
| kv             | Notes               |
| -------------- | ------------------- |
| cmd=REQ-SCAN   | Mandatory           |
| directory=path | Mandatory           |
| exclude=regex\<0x1f\>\[regex\...]  | Optional            |
## data
| kv                     | Notes     |
| ---------------------- | --------- |
| cmd=DATA-SCAN          | Mandatory |
| file=\<relative-path\> | Mandatory |
| size=\<size\>          | Mandatory |
| mtime=\<mtime\>        | Mandatory |
| permission=\<perm\>    | Mandatory |
| status=continue        | Mandatory |

DATA-SCAN repeats for each file. The REQ-SCAN is terminated by the RSP-SCAN below. Note: on the wire the exclude regexes are sent as a single `excludes` key, joined by 0x1f.

## response
RSP_SCAN indicates termination of scan data.

| kv                    | Notes                        |
| --------------------- | ---------------------------- |
| cmd=RSP-SCAN          | Mandatory                    |
| status=complete\|fail | Mandatory                    |
| text=\<text\>         | Error text if status\=\=fail |
# META
The hash request is only sent from client to server
## request

| kv                     | Notes     |     |
| ---------------------- | --------- | --- |
| cmd=REQ-META           | Mandatory |     |
| directory=\<path\>     | Mandatory |     |
| file=\<relative-path\> | Mandatory |     |

## response

| kv                     | Notes                                   |     |
| ---------------------- | --------------------------------------- | --- |
| cmd=RSP-META           | Mandatory                               |     |
| directory=\<path\>     | Mandatory                               |     |
| file=\<relative-path\> | Mandatory                               |     |
| hash=\<hash\>.         | Mandatory on success, optional on error |     |
| mtime=\<mtime\>        | Mandatory                               |     |
| permissions=\<perm\>   | Mandatory                               |     |
| size=\<size\>          | Mandatory                               |     |
| status=complete\|fail  | Mandatory                               |     |
| text="text"            | Optional, text if error                 |     |
# Read file
The read request is only sent from client to server. 
This is a request to transfer a file from the server to the client

## request
| kv                    | Notes                   |
| --------------------- | ----------------------- |
| cmd=REQ-READ          | Mandatory               |
| directory=\<path\>    | Mandatory               |
| file=\<relative-path\>| Mandatory               | 

## data
The server will sent these messages repeatedly until all data has been sent

| kv                     | Notes     |
| ---------------------- | --------- |
| cmd=DATA-CHUNK           | Mandatory |
| directory=\<path\>     | Mandatory |
| file=\<relative-path\> | Mandatory |
| total_size=\<size\>    | Mandatory |
| chunk_size=\<size\>    | Mandatory |
| status=continue        | Mandatory |
Chunk data immediatly follows this message

## Response
| kv                     | Notes     |
| ---------------------- | --------- |
| cmd=RSP-READ           | Mandatory |
| directory=\<path\>     | Mandatory |
| file=\<relative-path\> | Mandatory |
| hash=\<hash\>          | Mandatory if status\=\=complete |
| mtime=\<mtime\>        | Mandatory if status\=\=complete. Client applies it to the written file |
| status=\<complete\|fail\>    | Mandatory |
| test=\<text\>        | Optional, error text |

# Write file
The write file request is only sent from the client to the server.
This is a request to transfer a file from the client to the server.
## request
|kv|Notes|
|---|---|
|cmd=REQ-WRITE|Mandatory|
|directory=\<path\>|Mandatory|
|file=\<relative-path\>|Mandatory|
|hash=\<hash\> | Mandatory|
|mtime=\<mtime\> | Mandatory. Applied to the file once the hash verifies|

## data
See READ for the data chunk

## response
| kv                     | Notes     |
| ---------------------- | --------- |
| cmd=RSP-WRITE           | Mandatory |
| directory=\<path\>     | Mandatory |
| file=\<relative-path\> | Mandatory |
| status=complete\|fail        | Mandatory |
| text=\<text\>          |Optional, present on error

# Delete
The delete request is only sent from the client to the server
## request
|kv|Notes|
|---|---|
|cmd=REQ-DELETE|Mandatory|
|directory=\<path\>|Mandatory|
|file=\<relative-path\>|Mandatory|

## response
| kv                     | Notes     |
| ---------------------- | --------- |
| cmd=RSP-DELETE           | Mandatory |
| directory=\<path\>     |          |
| file=\<relative-path\> | Mandatory |
| status=complete\|fail        | Mandatory |
| text=\<text\>          |Optional, present on error


