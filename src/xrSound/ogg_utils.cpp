#include "StdAfx.h"

#include "ogg_utils.h"

size_t ov_read_func(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	IReader* F			= (IReader*)datasource; 
	size_t exist_block	= _max(0ul,iFloor(F->elapsed()/(float)size));
	size_t read_block	= _min(exist_block,nmemb);
	F->r				(ptr,(int)(read_block*size));	
	return read_block;
}

//	SEEK_SET	0	File beginning
//	SEEK_CUR	1	Current file pointer position
//	SEEK_END	2	End-of-file
int ov_seek_func(void *datasource, ogg_int64_t offset, int whence)
{
	switch (whence)
	{
	case SEEK_SET: ((IReader*)datasource)->seek((int)offset);	 break;
	case SEEK_CUR: ((IReader*)datasource)->advance((int)offset); break;
	case SEEK_END: ((IReader*)datasource)->seek((int)offset + ((IReader*)datasource)->length()); break;
	}
	return 0; 
}

int ov_close_func(void *datasource)
{
	return 1; /* Ignore close request so transport can be closed later */
}

long ov_tell_func(void *datasource)
{	
	return ((IReader*)datasource)->tell(); 
}

static bool ogg_page_header(const u8* p, size_t avail, u32& header_len, u32& body_len)
{
	if (avail < 27 || p[0]!='O' || p[1]!='g' || p[2]!='g' || p[3]!='S' || p[4]!=0)	return false;
	const u32 nseg	= p[26];
	if (avail < 27 + nseg)	return false;
	u32 body		= 0;
	for (u32 i = 0; i < nseg; ++i)	body += p[27 + i];
	header_len		= 27 + nseg;
	body_len		= body;
	return			true;
}

static u32 ogg_rd32(const u8* p)	{ return p[0] | (p[1]<<8) | (p[2]<<16) | (u32(p[3])<<24); }

bool ogg_quick_read(const u8* data, size_t size, ogg_quick_info& out)
{
	// The first two packets -- identification and comment -- assembled across pages by their lacing.
	static const u32	max_packet	= 4096;
	u8					packet[2][max_packet];
	u32					plen[2]		= {0, 0};
	u32					pidx		= 0;
	size_t				pos			= 0;
	u32					serial		= 0;
	bool				first		= true;
	while (pidx < 2)
	{
		u32 hl, bl;
		if (!ogg_page_header(data + pos, size - pos, hl, bl) || pos + hl + bl > size)	return false;
		const u8* page	= data + pos;
		const u32 s		= ogg_rd32(page + 14);
		if (first)		{ serial = s; first = false; }
		else if (s != serial)	return false;					// several logical streams: not a plain sound
		const u32 nseg	= page[26];
		const u8* body	= page + hl;
		u32 off			= 0;
		for (u32 i = 0; i < nseg && pidx < 2; ++i)
		{
			const u32 l		= page[27 + i];
			if (plen[pidx] + l > max_packet)	return false;
			CopyMemory		(packet[pidx] + plen[pidx], body + off, l);
			plen[pidx]		+= l;
			off				+= l;
			if (l < 255)	++pidx;
		}
		pos				+= hl + bl;
		if (pidx < 2 && pos >= size)	return false;
	}

	// identification header
	const u8* id		= packet[0];
	if (plen[0] < 30 || id[0] != 1 || memcmp(id + 1, "vorbis", 6) || ogg_rd32(id + 7) != 0)	return false;
	out.channels		= id[11];
	out.rate			= ogg_rd32(id + 12);
	if (!out.channels || !out.rate)	return false;

	// comment header: vendor string, then the user comments; the game keeps its block in the first
	const u8* c			= packet[1];
	const u32 clen		= plen[1];
	if (clen < 15 || c[0] != 3 || memcmp(c + 1, "vorbis", 6))	return false;
	u32 p				= 7;
	const u32 vlen		= ogg_rd32(c + p);	p += 4;
	if (vlen > clen || p + vlen + 4 > clen)	return false;
	p					+= vlen;
	const u32 count		= ogg_rd32(c + p);	p += 4;
	out.has_comment		= count > 0;
	out.comment_len		= 0;
	if (count)
	{
		if (p + 4 > clen)	return false;
		const u32 l		= ogg_rd32(c + p);	p += 4;
		if (l > clen || p + l > clen || l > sizeof(out.comment))	return false;
		CopyMemory		(out.comment, c + p, l);
		out.comment_len	= l;
	}

	// PCM length: granule position of the last page, i.e. the last "OggS" that parses and ends exactly
	// at the end of the file (an Ogg page is at most 27 + 255 + 255*255 bytes long).
	if (size < 27)		return false;
	const size_t lowest	= size > 65307 ? size - 65307 : 0;
	for (size_t i = size - 27; ; --i)
	{
		u32 hl, bl;
		if (data[i] == 'O' && ogg_page_header(data + i, size - i, hl, bl) && i + hl + bl == size)
		{
			if (ogg_rd32(data + i + 14) != serial)	return false;
			s64 g;
			CopyMemory	(&g, data + i + 6, 8);
			if (g < 0)	return false;
			out.pcm_total	= g;
			return		true;
		}
		if (i == lowest)	return false;
	}
}
