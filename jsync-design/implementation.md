We will start with the transfer engine
First step is to create the transfer class. This is used in both the client and server and does not contain any transport mechanism

```
struct Error
{
    std::string err;
    int ecode;
};

using ExpectBool = std::expected<bool, Error>;
using ExpectReadContext = std::expected<bool, ReadContext>;

struct Scaninfo
{
	std::string directory
	std::string rpath
	int permission;
	size_t size;
	time_t mtime;
};

struct ReadContext
{
	int fd;
	std::string directory;
	std::string rpath;
	size_t total_size;
}

struct Chunk
{
	std::string directory;
	std::string rpath;
	size_t total_size;
	size_t chunk_size;
	char bytes[MAX_CHUNK];
}

class Command
{
    ExpectBool scan(std::string& directory
                , std::vector<ScanInfo>&);
    ExpectBool hash(std::string& directory
                , std::string& rpath, std::string& hash);
    ExpectReadContext open(std::string& directory
                , std::string& rpath);
    ExpectBool read(const ReadContext&, Chunk&);
}
```