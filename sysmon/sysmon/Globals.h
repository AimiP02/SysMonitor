#include "pch.h"
#include "fastmutex.h"

struct Globals {
	void Init();
	void PushItem(LIST_ENTRY* entry);
	LIST_ENTRY* RemoveItem();


public:
	LIST_ENTRY ItemsHead;
	int ItemCount;
	FastMutex Mutex;
};