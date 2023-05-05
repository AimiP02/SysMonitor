const int MaxImageFileNameLength = 300;

enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

struct ItemHeader {
	ItemType Type;
	USHORT Size;
	LARGE_INTEGER Time;
};

struct ProcessExitInfo : ItemHeader {
	ULONG ProcessId;
};

struct ProcessCreateInfo : ItemHeader {
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
};

struct ThreadCreateExitInfo : ItemHeader {
	ULONG ProcessId;
	ULONG ThreadId;
};

struct ImageLoadInfo : ItemHeader {
	ULONG ProcessId;
	ULONG64 ImageBase;
	ULONG ImageSize;
	WCHAR ImageFileName[MaxImageFileNameLength + 1];
};

template <typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};