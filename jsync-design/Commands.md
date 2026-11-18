# Write format
This describes the wire protocol for each command. The protocol is an ASCII based semi-colon delimited key=value pairs on a single line. 

The commands that take a directory=\<...\> and file=\<relative-path\> are in the remote server file paths. It is the responsibilty of the client to convert into local paths.

## Fields
The value are all transmitted in ASCII, no binary except for the file chunks.

The format of the wire is as follows
JS\<version\>;      This is the introduction. The version will be 1.0. The length of this field is always 5 bytes
\<length\>;           The length in bytes of the remaining data. This length does not include any chunk data. The length of this field is always 8 bytes
\<command\>;     The request or response command
key=value;...        The key value pairs, these can be considered arguments to the command

Since the JS and length are fixed size, a read of 15 bytes will always retrive the JS\<version\>;\<length\>;
The JS tag can be used to check alignment and the length will indicate the next read length to get the command and kv pairs.

## KV Fields

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
JS1.0;\<length\>;REQ-SCAN;directory=path;\[exclude=regex;...\]
## response
JS1.0;\<length\>;RSP-SCAN;file=\<relative-path\>;size=\<size\>;mtime=\<mtime\>;permission=\<permission\>
.
.
.
JS1.0;\<length\>;RSP-SCAN;status=complete;text=<text_if_required>
# HASH
## request
JS1.0;\<length\>;REQ-HASH;directory=\<path\>;file=\<relative-path\>
## response
JS1.0;\<length\>;RSP-HASH;directory=\<path\>;file=\<relative-path\>;hash=\<hash\>;status=complete;text=<text_if_error>
# Read file

## request
JS1.0;\<length\>;REQ-READ;directory=\<path\>;file=\<relative-path\>
## response
JS1.0;\<length\>;RSP-READ-CHUNK;directory=\<path\>;file=\<relative-path\>;total-size=\<size\>;chunck=\<size\>
.
.
.
JS1.0;\<length\>;RSP-READ;directory=\<path\>;file=\<relative-path\>;hash=\<hash\>;status=complete;text=<text_if_error>

# Write file
## request
JS1.0;\<length\>;REQ-WRITE;directory=\<path\>;file=\<relative-path\>;total=\<size\>;chunk=\<size\>;hash=\<hash\>
chunck bytes
JS1.0;\<length\>REQ-WRITE-CHUNK;directory=\<path\>;file=\<relative-path\>;chunk=\<size\>
chunck bytes
.
.
.
JS1.0;\<length\>;REQ-WRITE-CHUNK;directory=\<path\>;file=\<relative-path\>;status=complete;
## response
JS1.0;\<length\>;RSP-WRITE;directory=\<path\>;file=\<relative-path\>size=\<size\>;hash=\<hash\>;status=compete;text=<text_if_error>

# Delete
## request
JS1.0;\<length\>;REQ-DELETE;directory=\<path\>;file=\<relative-path\>
## response
JS1.0;\<length\>;RSP-DELETE;directory=\<path\>;file=\<relative-path\>;status=complete;text=<text_if_error>



