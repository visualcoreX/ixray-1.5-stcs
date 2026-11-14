#include "stdafx.h"
#include <malloc.h>
#include <errno.h>

XRCORE_API void vminfo (size_t *_free, size_t *reserved, size_t *committed) {
	MEMORY_BASIC_INFORMATION memory_info;
	memory_info.BaseAddress = nullptr;
	*_free = *reserved = *committed = 0;
	while (VirtualQuery (memory_info.BaseAddress, &memory_info, sizeof (memory_info))) {
		switch (memory_info.State) {
		case MEM_FREE:
			*_free		+= memory_info.RegionSize;
			break;
		case MEM_RESERVE:
			*reserved	+= memory_info.RegionSize;
			break;
		case MEM_COMMIT:
			*committed += memory_info.RegionSize;
			break;
		}
		memory_info.BaseAddress = (char *) memory_info.BaseAddress + memory_info.RegionSize;
	}
}

XRCORE_API void log_vminfo	()
{
	size_t  w_free, w_reserved, w_committed;
	vminfo	(&w_free, &w_reserved, &w_committed);
	Msg		(
		"* [win32]: free[%d K], reserved[%d K], committed[%d K]",
		w_free/1024,
		w_reserved/1024,
		w_committed/1024
	);
}

// ---- A broken heap -------------------------------------------------------------------------------------------
// _heapwalk is one HeapWalk step per call and takes no lock across the calls, so a walk that runs while another
// thread allocates or frees (sound, resource loading, the Discord SDK -- all busy at a load phase) can trip over a
// block changing under it and report _HEAPBADNODE on a perfectly healthy heap. That is what the 'bad node in heap'
// FATALs at the first load phase were: the diagnostic's own second walk ended with _HEAPEND, and the "damage" it
// pointed at was the zeroed page tail after the heap's last block (a >508K block, which Windows allocates on its
// own). So the walk now holds the heap lock, and a failure is confirmed with HeapValidate before the game is
// stopped. A real corruption still FATALs, with the last good block and the bytes where the next one should start
// in the log: whatever is written there in place of a header often names the data that ran over it.
static bool heap_read16(const u8* p, u8* out)
{
	__try	{ for (int i = 0; i < 16; ++i) out[i] = p[i]; }
	__except(EXCEPTION_EXECUTE_HANDLER)	{ return false; }
	return true;
}

namespace
{
	const u32	HEAP_DUMP_ROWS	= 12;

	// Everything the report needs, gathered while the heap is still locked. Nothing is logged under the lock:
	// Msg takes the log's own lock, and a thread holding that one while waiting for the heap would deadlock us.
	struct heap_damage
	{
		_HEAPINFO	last;					// the last block the walk got through
		u32			good;					// how many it got through
		BOOL		valid;					// HeapValidate's verdict on the whole heap
		u8			bytes[HEAP_DUMP_ROWS][16];
		bool		readable[HEAP_DUMP_ROWS];
	};

	void capture_damage(HANDLE heap, const _HEAPINFO& last, u32 good, heap_damage& d)
	{
		d.last		= last;
		d.good		= good;
		d.valid		= heap ? HeapValidate(heap, 0, NULL) : FALSE;
		const u8*	end		= last._pentry ? (const u8*)last._pentry + last._size : NULL;
		for (u32 row = 0; row < HEAP_DUMP_ROWS; ++row)
			d.readable[row]	= end && heap_read16(end - 64 + row*16, d.bytes[row]);
	}

	void report_damage(const char* what, int status, const heap_damage& d)
	{
		const u8*	end		= d.last._pentry ? (const u8*)d.last._pentry + d.last._size : NULL;

		if (d.valid)
		{
			// HeapValidate went through every block with the lock held and found nothing: the walk is what failed.
			Msg("~ [heap] the walk reported '%s' (status %d) after %u blocks, but HeapValidate finds the heap intact -- ignored",
				what, status, d.good);
			return;
		}

		Msg("! [heap] %s: the walk stops with status %d after %u good blocks, and HeapValidate confirms it", what, status, d.good);
		if (end)
		{
			Msg("! [heap] last good block %p, %u bytes, %s; the bad one starts at %p (offset +0 below)",
				d.last._pentry, (u32)d.last._size, d.last._useflag == _USEDENTRY ? "in use" : "free", end);
			for (u32 row = 0; row < HEAP_DUMP_ROWS; ++row)
			{
				const u8*	p	= end - 64 + row*16;
				if (!d.readable[row])	{ Msg("! [heap]   %+5d %p: <unreadable>", int(p - end), p); break; }
				string128	hex;	hex[0] = 0;
				char		asc[17];
				for (int i = 0; i < 16; ++i)
				{
					string16	h;	xr_sprintf(h, "%02X ", d.bytes[row][i]);	xr_strcat(hex, h);
					asc[i]		= (d.bytes[row][i] >= 32 && d.bytes[row][i] < 127) ? char(d.bytes[row][i]) : '.';
				}
				asc[16]		= 0;
				Msg("! [heap]   %+5d %p: %s|%s|", int(p - end), p, hex, asc);
			}
		}
		FlushLog();
		if (end)	Debug.fatal(DEBUG_INFO, "%s: the damage starts at %p, right after a good %u-byte block at %p, %u good blocks into the heap (the bytes there are in the log)",
						what, end, (u32)d.last._size, d.last._pentry, d.good);
		else		Debug.fatal(DEBUG_INFO, "%s: at the very start of the heap", what);
	}
}

u32	mem_usage_impl	(u32* pBlocksUsed, u32* pBlocksFree)
{
	// The CRT heap _heapwalk walks. The lock is recursive, so _heapwalk's own HeapValidate/HeapWalk calls inside it
	// are fine; every other thread that allocates simply waits the few milliseconds the walk takes.
	HANDLE			heap	= (HANDLE)_get_heap_handle();
	const bool		locked	= heap && HeapLock(heap);

	_HEAPINFO		hinfo;
	_HEAPINFO		last;	last._pentry = NULL;	last._size = 0;	last._useflag = 0;
	int				heapstatus;
	hinfo._pentry	= NULL;
	size_t	total	= 0;
	u32	blocks_free	= 0;
	u32	blocks_used	= 0;
	while( ( heapstatus = _heapwalk( &hinfo ) ) == _HEAPOK )
	{
		last			= hinfo;
		if (hinfo._useflag == _USEDENTRY)	{
			total		+= hinfo._size;
			blocks_used	+= 1;
		} else {
			blocks_free	+= 1;
		}
	}

	const char*		what	= NULL;
	switch( heapstatus )
	{
	case _HEAPBADPTR:	what = "bad pointer to heap";	break;
	case _HEAPBADBEGIN:	what = "bad start of heap";		break;
	case _HEAPBADNODE:	what = "bad node in heap";		break;
	default:			break;		// _HEAPEND, _HEAPEMPTY: a complete walk
	}

	heap_damage		damage;
	if (what)		capture_damage(heap, last, blocks_used + blocks_free, damage);

	if (locked)		HeapUnlock(heap);

	if (what)		report_damage(what, heapstatus, damage);

	if (pBlocksFree)	*pBlocksFree= 1024*(u32)blocks_free;
	if (pBlocksUsed)	*pBlocksUsed= 1024*(u32)blocks_used;
	return (u32) total;
}

u32		xrMemory::mem_usage		(u32* pBlocksUsed, u32* pBlocksFree)
{
	return				(mem_usage_impl(pBlocksUsed,pBlocksFree));
}
