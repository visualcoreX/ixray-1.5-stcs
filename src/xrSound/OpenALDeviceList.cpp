/*
 * Copyright (c) 2005, Creative Labs Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided
 * that the following conditions are met:
 * 
 *     * Redistributions of source code must retain the above copyright notice, this list of conditions and
 * 	     the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 * 	     and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *     * Neither the name of Creative Labs Inc. nor the names of its contributors may be used to endorse or
 * 	     promote products derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "stdafx.h"

#include "OpenALDeviceList.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <objbase.h>
#pragma warning(pop)

#ifdef _EDITOR
	log_fn_ptr_type*	pLog = NULL;

void __cdecl al_log(char* msg)
{
	Log(msg);
}
#endif

ALDeviceList::ALDeviceList()
{
#ifdef _EDITOR
	pLog					= al_log;
#endif

	snd_device_id			= u32(-1);
	Enumerate();
}

/* 
 * Exit call
 */
ALDeviceList::~ALDeviceList()
{
	for( int i=0; snd_devices_token[i].name; i++ )
	{
		xr_free					(snd_devices_token[i].name);
	}
	xr_free						(snd_devices_token);
	snd_devices_token			= NULL;
}


// OpenAL hands device names over as UTF-8, while the menu fonts and every config file in this
// build are cp1251 -- so a Cyrillic endpoint name arrives as two bytes per letter and gets drawn
// one glyph per byte. Convert for DISPLAY only; alcOpenDevice must get the original bytes back.
// cp1251 flat rather than CP_ACP: the fonts are cp1251 whatever the machine's ANSI page is.
static void	snd_utf8_to_ansi(LPCSTR src, string256& dst)
{
	xr_strcpy(dst, src);					// keep the original if any step below fails

	wchar_t	wide[256];
	if (0 == MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, wide, 256))
		return;								// not valid UTF-8 -- some other driver, leave it be

	char	ansi[256];
	if (0 == WideCharToMultiByte(1251, 0, wide, -1, ansi, sizeof(ansi), "?", nullptr))
		return;

	xr_strcpy(dst, ansi);
}

// "OpenAL Soft on Speakers (Realtek(R) Audio)" -> "Speakers (Realtek(R) Audio)". Every entry
// carries the same prefix, so it is pure noise in a 217 px combo.
static LPCSTR	snd_display_name(LPCSTR nm)
{
	static const char* pfx = "OpenAL Soft on ";
	const size_t len = xr_strlen(pfx);
	return (0 == strncmp(nm, pfx, len)) ? nm + len : nm;
}

void ALDeviceList::Enumerate()
{
	char				*devices;
	int	ALmajor, ALminor, EFXmajor, EFXminor;
	
	Msg("SOUND: OpenAL: enumerate devices...");
	// have a set of vectors storing the device list, selection status, spec version #, and XRAM support status
	// -- empty all the lists and reserve space for 10 devices
	m_devices.clear				();
	
	CoUninitialize();

	// OpenAL Soft answers ALC_DEVICE_SPECIFIER with exactly one name -- "OpenAL Soft", its own
	// wrapper around whatever Windows currently calls the default endpoint. The real endpoints are
	// behind ALC_ENUMERATE_ALL_EXT, so ask for those whenever the driver has them and keep the old
	// list as the fallback for drivers that do not.
	const bool	all_ext	= !!alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT");
	const bool	one_ext	= !!alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT");

	if (all_ext || one_ext)
	{
		const ALCenum	e_list		= all_ext ? ALC_ALL_DEVICES_SPECIFIER		: ALC_DEVICE_SPECIFIER;
		const ALCenum	e_default	= all_ext ? ALC_DEFAULT_ALL_DEVICES_SPECIFIER	: ALC_DEFAULT_DEVICE_SPECIFIER;
		Msg("SOUND: OpenAL: %s present", all_ext ? "EnumerateAllExtension" : "EnumerationExtension");

		// kept raw: it is matched against ALDeviceDesc::name, which is raw too. Only the log line
		// gets the readable form.
		xr_strcpy(m_defaultDeviceName, (char*)alcGetString(nullptr, e_default));
		string256	default_readable;	snd_utf8_to_ansi(m_defaultDeviceName, default_readable);
		Msg("SOUND: OpenAL: system default SndDevice name is %s", default_readable);

		// First entry is not a device. It opens with nullptr -- "whatever the OS calls default right
		// now" -- so it follows the player changing the default endpoint in Windows instead of pinning
		// the one that happened to be default when they first ran the game. This is what a profile
		// that has never chosen anything gets.
		if (ALCdevice* def = alcOpenDevice(nullptr))
		{
			ALmajor = 1; ALminor = 1; EFXmajor = 0; EFXminor = 0;
			if (ALCcontext* ctx = alcCreateContext(def, nullptr))
			{
				alcMakeContextCurrent(ctx);
				alcGetIntegerv(def, ALC_MAJOR_VERSION, sizeof(int), &ALmajor);
				alcGetIntegerv(def, ALC_MINOR_VERSION, sizeof(int), &ALminor);
				alcGetIntegerv(def, ALC_EFX_MAJOR_VERSION, sizeof(int), &EFXmajor);
				alcGetIntegerv(def, ALC_EFX_MINOR_VERSION, sizeof(int), &EFXminor);
				alcMakeContextCurrent(nullptr);
				alcDestroyContext(ctx);
			}
			alcCloseDevice(def);

			ALDeviceDesc	desc(SND_DEVICE_DEFAULT, ALminor, ALmajor, EFXminor, EFXmajor);
			desc.system_default	= true;
			m_devices.push_back(desc);
		}

		devices = (char*)alcGetString(nullptr, e_list);
		// each device terminated with a single NULL, list terminated with double NULL
		while (devices && *devices != '\0')
		{
			ALCdevice *device		= alcOpenDevice(devices);
			if (device) 
			{
				ALCcontext* context = alcCreateContext(device, nullptr);
				if (context) 
				{
					alcMakeContextCurrent(context);

					alcGetIntegerv(device, ALC_MAJOR_VERSION, sizeof(int), &ALmajor);
					alcGetIntegerv(device, ALC_MINOR_VERSION, sizeof(int), &ALminor);

					alcGetIntegerv(device, ALC_EFX_MAJOR_VERSION, sizeof(int), &EFXmajor);
					alcGetIntegerv(device, ALC_EFX_MINOR_VERSION, sizeof(int), &EFXminor);

					// keep the ENUMERATED string, not what the opened device reports back: this is the
					// one alcOpenDevice is known to accept when the player picks this entry later
					ALDeviceDesc	desc(devices, ALminor, ALmajor, EFXminor, EFXmajor);
					string256		readable;	snd_utf8_to_ansi(devices, readable);
					xr_strcpy(desc.display, snd_display_name(readable));
					m_devices.push_back(desc);

					alcMakeContextCurrent(nullptr);	// destroying the CURRENT context is undefined
					alcDestroyContext(context);
				}else
				{
					Msg("SOUND: OpenAL: cant create context for %s",devices);
				}
				alcCloseDevice(device);
			}else
			{
				Msg("SOUND: OpenAL: cant open device %s",devices);
			}

			devices		+= xr_strlen(devices) + 1;
		}
	}else
		Msg("SOUND: OpenAL: no enumeration extension present");

//make token
	u32 _cnt								= GetNumDevices();
	snd_devices_token						= xr_alloc<xr_token>(_cnt+1);
	snd_devices_token[_cnt].id				= -1;
	snd_devices_token[_cnt].name = nullptr;
	for(u32 i=0; i<_cnt;++i)
	{
		snd_devices_token[i].id				= i;
		// the token name is both what the menu shows and what goes into user.ltx
		snd_devices_token[i].name			= xr_strdup(m_devices[i].display);
	}
//--

	if(0!=GetNumDevices())
		Msg("SOUND: OpenAL: All available devices:");

	for (u32 j = 0; j < GetNumDevices(); j++)
	{
		GetDeviceVersion(j, &ALmajor, &ALminor, &EFXmajor, &EFXminor);
		// Assume EFX by default, we only care about the spec version.
		Msg("%d. %s, Spec Version %d.%d, EFX Spec Version %d.%d",
			j+1, 
			GetDeviceName(j), 
			ALmajor,
			ALminor,
			EFXmajor,
			EFXminor
			);
	}
}

LPCSTR ALDeviceList::GetDeviceName(u32 index)
{
	return snd_devices_token[index].name;
}

void ALDeviceList::SelectBestDevice()
{
	// snd_device_id is u32(-1) when the profile has never chosen a device, and also when it named
	// one that no longer exists -- CCC_Token leaves the value alone on a name it cannot match.
	if(snd_device_id==u32(-1))
	{
		u32 new_device_id		= u32(-1);

		// the "system default" stand-in, whenever probing it worked
		for (u32 i = 0; i < GetNumDevices(); ++i)
			if (m_devices[i].system_default)	{ new_device_id = i; break; }

		// no stand-in: take the endpoint the driver itself calls default. (The old code picked the
		// highest AL version among entries with that same name, which only ever mattered for the
		// duplicate entries the Creative-era enumeration produced.)
		if (new_device_id==u32(-1))
			for (u32 i = 0; i < GetNumDevices(); ++i)
				if (0==_stricmp(m_defaultDeviceName, m_devices[i].name))	{ new_device_id = i; break; }

		if(new_device_id==u32(-1) )
		{
			R_ASSERT(GetNumDevices()!=0);
			new_device_id = 0; //first
		};
		snd_device_id = new_device_id;
	}
	if(GetNumDevices()==0)
		Msg("SOUND: Can't select device. List empty");
	else
		Msg("SOUND: Selected device is %s", GetDeviceName(snd_device_id));
}

void ALDeviceList::GetDeviceVersion(u32 index, int* ALmajor, int* ALminor, int* EFXmajor, int* EFXminor)
{
	*ALmajor = m_devices[index].ALmajor_ver;
	*ALminor = m_devices[index].ALminor_ver;
	*EFXmajor = m_devices[index].EFXmajor_ver;
	*EFXminor = m_devices[index].EFXminor_ver;
	return;
}
