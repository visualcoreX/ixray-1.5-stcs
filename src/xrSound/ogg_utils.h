#ifndef _OGG_UTILS_H_INCLUDED_
#define _OGG_UTILS_H_INCLUDED_

#pragma once

size_t ov_read_func(void *ptr, size_t size, size_t nmemb, void *datasource);
int ov_seek_func(void *datasource, ogg_int64_t offset, int whence);
int ov_close_func(void *datasource);
long ov_tell_func(void *datasource);

// What CSoundRender_Source needs from an Ogg Vorbis file, read straight from the pages without
// starting a decoder: channels and rate (identification header), the first user comment (the game's
// distances/volume block) and the PCM length (granule position of the last page). ov_open does the
// same plus the full codebook setup and a bisection to the end -- ~0.08 ms a file, and the first NPC
// with a new voice set adds ~300 sources at once (~24 ms in one frame). Checked against libvorbis on
// all 3585 game sounds: identical. Returns false on anything unexpected; the caller then uses ov_open.
struct ogg_quick_info
{
	u32			channels;
	u32			rate;
	s64			pcm_total;
	bool		has_comment;
	u32			comment_len;
	u8			comment[4096];
};
bool ogg_quick_read(const u8* data, size_t size, ogg_quick_info& out);

#endif
