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
// 'bad node in heap' is only FOUND where the CRT heap is next walked -- here, at the first load phase after a level
// unloads -- long after whatever wrote over a block's header. The walk below stops right at the damage, so the
// report logs the last good block and the bytes around where the next one should start: whatever is written there
// in place of a header often names the data that ran over it (a string, a file or texture name, floats, pointers).
static bool heap_read16(const u8* p, u8* out)
{
	__try	{ for (int i = 0; i < 16; ++i) out[i] = p[i]; }
	__except(EXCEPTION_EXECUTE_HANDLER)	{ return false; }
	return true;
}

static void heap_broken(const char* what)
{
	_HEAPINFO	hi;		hi._pentry = NULL;
	_HEAPINFO	last;	last._pentry = NULL;	last._size = 0;	last._useflag = 0;
	int			st;
	u32			good	= 0;
	while ((st = _heapwalk(&hi)) == _HEAPOK)	{ last = hi; ++good; }
	Msg("! [heap] %s: the walk stops with status %d after %u good blocks", what, st, good);
	const u8*	end		= last._pentry ? (const u8*)last._pentry + last._size : NULL;
	if (end)
	{
		Msg("! [heap] last good block %p, %u bytes, %s; the bad one starts at %p (offset +0 below)",
			last._pentry, (u32)last._size, last._useflag == _USEDENTRY ? "in use" : "free", end);
		const u8*	from	= end - 64;
		for (u32 row = 0; row < 12; ++row)
		{
			const u8*	p	= from + row*16;
			u8			b[16];
			if (!heap_read16(p, b))	{ Msg("! [heap]   %+5d %p: <unreadable>", int(p - end), p); break; }
			string128	hex;	hex[0] = 0;
			char		asc[17];
			for (int i = 0; i < 16; ++i)
			{
				string16	h;	xr_sprintf(h, "%02X ", b[i]);	xr_strcat(hex, h);
				asc[i]		= (b[i] >= 32 && b[i] < 127) ? char(b[i]) : '.';
			}
			asc[16]		= 0;
			Msg("! [heap]   %+5d %p: %s|%s|", int(p - end), p, hex, asc);
		}
	}
	FlushLog();
	if (end)	Debug.fatal(DEBUG_INFO, "%s: the damage starts at %p, right after a good %u-byte block at %p, %u good blocks into the heap (the bytes there are in the log)",
					what, end, (u32)last._size, last._pentry, good);
	else		Debug.fatal(DEBUG_INFO, "%s: at the very start of the heap", what);
}

u32	mem_usage_impl	(u32* pBlocksUsed, u32* pBlocksFree)
{
	_HEAPINFO		hinfo;
	int				heapstatus;
	hinfo._pentry	= NULL;
	size_t	total	= 0;
	u32	blocks_free	= 0;
	u32	blocks_used	= 0;
	while( ( heapstatus = _heapwalk( &hinfo ) ) == _HEAPOK )
	{ 
		if (hinfo._useflag == _USEDENTRY)	{
			total		+= hinfo._size;
			blocks_used	+= 1;
		} else {
			blocks_free	+= 1;
		}
	}
	if (pBlocksFree)	*pBlocksFree= 1024*(u32)blocks_free;
	if (pBlocksUsed)	*pBlocksUsed= 1024*(u32)blocks_used;

	switch( heapstatus )
	{
	case _HEAPEMPTY:
		break;
	case _HEAPEND:
		break;
	case _HEAPBADPTR:
		heap_broken		("bad pointer to heap");
		break;
	case _HEAPBADBEGIN:
		heap_broken		("bad start of heap");
		break;
	case _HEAPBADNODE:
		heap_broken		("bad node in heap");
		break;
	}
	return (u32) total;
}

u32		xrMemory::mem_usage		(u32* pBlocksUsed, u32* pBlocksFree)
{
	return				(mem_usage_impl(pBlocksUsed,pBlocksFree));
}
