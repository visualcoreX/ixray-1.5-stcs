#ifndef ALDEVICELIST_H
#define ALDEVICELIST_H

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/efx.h"

#define AL_GENERIC_HARDWARE "Generic Hardware"
#define AL_GENERIC_SOFTWARE "Generic Software"

// Name of the synthetic first entry. The menu runs combo items through the string table, so
// this is a string id (ui_st_mm.xml has the text); real device names have no entry there and
// come through unchanged. It is also what lands in user.ltx, and it stays valid whatever the
// machine's audio hardware turns into.
#define SND_DEVICE_DEFAULT	"snd_device_default"

struct ALDeviceDesc{
	string256 name;			// what alcOpenDevice takes; empty-meaning for the default entry
	string256 display;		// what the menu shows: name minus OpenAL Soft's common prefix
	bool	  system_default;	// entry 0: not a device, "whatever the OS calls default"
	int	ALminor_ver;
	int	ALmajor_ver;
	int	EFXminor_ver;
	int	EFXmajor_ver;

	union ESndProps
	{
		struct{
			u16				selected	:1;
			u16				efx			:1;
			u16 xra : 1;

			u16				unused		:9;
		};
		u16 storage;
	};
	ESndProps				props;
	ALDeviceDesc(LPCSTR nm, int almn, int almj, int efxmn, int efxmj)
	{
		xr_strcpy(name, nm);
		xr_strcpy(display, nm);
		system_default = false;
		ALminor_ver = almn;
		ALmajor_ver = almj;
		EFXminor_ver = efxmn;
		EFXmajor_ver = efxmj;
		props.storage = 0;
		//props.eax_unwanted=true;
	}
};

class ALDeviceList
{
private:
	xr_vector<ALDeviceDesc>	m_devices;
	string256			m_defaultDeviceName;
	void				Enumerate				();
public:
						ALDeviceList			();
						~ALDeviceList			();

	u32					GetNumDevices			()				{return m_devices.size();}
	const ALDeviceDesc&	GetDeviceDesc			(u32 index)		{return m_devices[index];}
	LPCSTR				GetDeviceName			(u32 index);
	void GetDeviceVersion(u32 index, int* ALmajor, int* ALminor, int* EFXmajor, int* EFXminor);
	void				SelectBestDevice		();
};

#endif // ALDEVICELIST_H
