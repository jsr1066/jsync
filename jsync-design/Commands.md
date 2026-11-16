All commands that take a directory=<...> and file=\<relative-path\> are in the remote server file paths. It is the responsibilty of the client to convert into local paths

# SCAN
## request
SCAN:directory=path;\[exclude=regex;...\]
## response
SCAN:file=\<relative-path\>;size=\<size\>;mtime=\<mtime\>;permission=\<permission\>
.
.
.
SCAN:status=complete
# HASH
## request
HASH:directory=\<path\>;file=\<relative-path\>
## response
HASH:directory=\<path\>;file=\<relative-path\>;hash=\<hash\>
# Read file
## request
READ:directory=\<path\>;file=\<relative-path\>
## response
READ:directory=\<path\>;file=\<relative-path\>;total-size=\<size\>;chunck=\<size\>
.
.
.
READ:directory=\<path\>;file=\<relative-path\>;status=complete

# Write file
## request
WRITE:directory=\<path\>;file=\<relative-path\>;total=\<size\>;chunk=\<size\>
chunck bytes
WRITE:directory=\<path\>;file=\<relative-path\>;total=\<size\>;chunk=\<size\>
chunck bytes
.
.
.
WRITE:directory=\<path\>;file=\<relative-path\>;status=complete
## response
WRITE:directory=\<path\>;file=\<relative-path\>;status=complete;size=\<size\>;hash=\<hash\>

# Rename temp
## request
RENAME:directory=\<path\>;file=\<relative-path\>;
## response
RENAME:status=complete
# Delete
## request
DELETE:directory=\<path\>;file=\<relative-path\>
## response
DELETE:directory=\<path\>;file=\<relative-path\>;status=complete



