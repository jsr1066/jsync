This describes the wire protocol for each command. The protocol is an ASCII based semi-colon delimited key=value pairs on a single line. 

The commands that take a directory=\<...\> and file=\<relative-path\> are in the remote server file paths. It is the responsibilty of the client to convert into local paths.

KV fields

| fieldname  | value                 | Note                                 |
| ---------- | --------------------- | ------------------------------------ |
| directory  | string                | Specifies the server side directory  |
| file       | string                | The relative path to the directory   |
| exclude    | regex                 | Pattern of files to exclude          |
| hash       | hex                   | File hash                            |
| total_size | int                   | Size of file                         |
| chunk_size | int                   | Size of file data following the line |
| status     | "complete"<br>"error" | Completion status<br>                |
| text       | string                | Error message                        |
| mtime      | int                   | time_t value for modification time   |
| permission | int                   | file permission                      |


# SCAN
## request
REQ-SCAN:directory=path;\[exclude=regex;...\]
## response
RSP-SCAN:file=\<relative-path\>;size=\<size\>;mtime=\<mtime\>;permission=\<permission\>
.
.
.
RSP-SCAN:status=complete;text=<text_if_required>
# HASH
## request
REQ-HASH:directory=\<path\>;file=\<relative-path\>
## response
RSP-HASH:directory=\<path\>;file=\<relative-path\>;hash=\<hash\>
# Read file

## request
REQ-READ:directory=\<path\>;file=\<relative-path\>
## response
RSP-READ-CHUNK:directory=\<path\>;file=\<relative-path\>;total-size=\<size\>;chunck=\<size\>
.
.
.
RSP-READ:directory=\<path\>;file=\<relative-path\>;hash=\<hash\>;status=complete;text=<text_if_error>

# Write file
## request
REQ-WRITE:directory=\<path\>;file=\<relative-path\>;total=\<size\>;chunk=\<size\>;hash=\<hash\>
chunck bytes
REQ-WRITE-CHUNK:directory=\<path\>;file=\<relative-path\>;chunk=\<size\>
chunck bytes
.
.
.
REQ-WRITE-CHUNK:directory=\<path\>;file=\<relative-path\>;status=complete;
## response
RSP-WRITE:directory=\<path\>;file=\<relative-path\>size=\<size\>;hash=\<hash\>;status=compete;text=<text_if_error>

# Delete
## request
REQ-DELETE:directory=\<path\>;file=\<relative-path\>
## response
RSP-DELETE:directory=\<path\>;file=\<relative-path\>;status=complete;text=<text_if_error>



