# Api
An interface is provided for implementing a user interface for jsync.
The initial (and possibly) only interface is a terminal commands
## Database functions

```
ExpectedDBContext create_db(const std::string& path_to_database, const std::string& ssh_host_connect);
```

```
ExpectedDBContext open_db(const std::string& path_to_db);
```

```
ExpectedBool add_path(DBContext, const std::string* local_path, const std::string& remote_path);
```

```
ExpectedBool add_exclude(DBContext, const std::string& regex);
```

## File functions
```
ExpectedBool scan(std::vector<FileInfo>& files);
ExpectedBool scan(Peer&, std::vector<FileInfo>& files);
```

```
ExpectedBool meta(const std::string& dir, const std::string& rpath, FileInfo&);
ExpectedBool meta(Peer, const std::string& dir, const std::string& rpath, FileInfo&);
```