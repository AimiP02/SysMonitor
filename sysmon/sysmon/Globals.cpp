#include "pch.h"
#include "fastmutex.h"
#include "Globals.h"
#include "SysmonCommon.h"

void Globals::Init() {
	InitializeListHead(&ItemsHead);
	Mutex.Init();
	ItemCount = 0;
}

void Globals::PushItem(LIST_ENTRY* entry) {
	AutoLock<FastMutex> lock(Mutex);
	if (ItemCount > 1024) {
		auto head = RemoveHeadList(&ItemsHead);
		ExFreePool(CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry));
		ItemCount--;
	}
	InsertTailList(&ItemsHead, entry);
	ItemCount++;
}

LIST_ENTRY* Globals::RemoveItem() {
	AutoLock<FastMutex> lock(Mutex);
	auto head = RemoveHeadList(&ItemsHead);
	if (head == &ItemsHead)
		return nullptr;
	ItemCount--;
	return head;
}