//=============================================================================
//  Filename:   UIColorAnimatorWrapper.cpp
//	Created by Roman E. Marchenko, vortex@gsc-game.kiev.ua
//	Copyright 2004. GSC Game World
//	---------------------------------------------------------------------------
//  Оберточка над LightAnimLibrary, в которой реализуется более удобная
//	работа с анимацией, и прикручен апдейт времени специфичный для UI: так
//	как UI апдейтится не на кажом кадре, то кормить функции интерполяции
//	Device.fTimeDelta нельзя - анимация тормознутая получается. Приходится 
//	постоянно считать время между апдейтами.
//=============================================================================

#include "stdafx.h"
#include "UIColorAnimatorWrapper.h"
#include "../../xrEngine/LightAnimLibrary.h"

// ---- A color animation the library does not hold ------------------------------------------------------
// LALib is read from lanims.xr once at startup and nothing changes it afterwards. A UI animation that is in
// the file and is not found later (a player's crash: "ui_map_area_anim", hours into a game, on a HUD text)
// therefore means the library's records were overwritten in memory. A blinking colour is not worth the game:
// the caller goes on without the animation, and the log gets what the library holds, so the next report says
// what was overwritten and with what -- for every record whose name is no longer a sane string, the bytes of
// its shared string (header: refcount, length, crc, next; then the text).
static bool lanim_read(const void* p, u8* out, u32 n)
{
	__try	{ for (u32 i = 0; i < n; ++i) out[i] = ((const u8*)p)[i]; }
	__except(EXCEPTION_EXECUTE_HANDLER)	{ return false; }
	return true;
}

static bool lanim_item_name(const CLAItem* I, const char*& name)
{
	__try	{ name = I->cName.c_str(); }
	__except(EXCEPTION_EXECUTE_HANDLER)	{ return false; }
	return true;
}

static void lanim_hex(LPCSTR what, const void* p, u32 n)
{
	u8			b[64];
	if (n > sizeof(b))	n = sizeof(b);
	if (!p || !lanim_read(p, b, n))	{ Msg("! [lanim]     %s %p: <unreadable>", what, p); return; }
	for (u32 row = 0; row < n; row += 16)
	{
		string128	hex;	hex[0] = 0;
		char		asc[17];
		u32			cnt	= _min(16u, n - row);
		for (u32 i = 0; i < cnt; ++i)
		{
			string16	h;	xr_sprintf(h, "%02X ", b[row + i]);	xr_strcat(hex, h);
			asc[i]		= (b[row + i] >= 32 && b[row + i] < 127) ? char(b[row + i]) : '.';
		}
		asc[cnt]	= 0;
		Msg("! [lanim]     %s %p: %s|%s|", what, (const u8*)p + row, hex, asc);
	}
}

static void lanim_report_miss(LPCSTR wanted)
{
	static u32	reports	= 0;
	if (reports >= 3)	return;		// a HUD text asks every frame: the first few say it all
	++reports;

	const u32	cnt		= (u32)LALib.Items.size();
	Msg("! [lanim] color animation [%s] is not in the light animation library (%u records): it is in lanims.xr, "
		"so the library was overwritten in memory -- going on without the animation", wanted, cnt);
	if (cnt > 4096)	{ Msg("! [lanim]   the record count itself is garbage"); FlushLog(); return; }

	u32			sane	= 0;
	for (u32 i = 0; i < cnt; ++i)
	{
		CLAItem*	I		= LALib.Items[i];
		const char*	name	= NULL;
		if (!I || !lanim_item_name(I, name))
		{
			Msg("! [lanim]   record %u: item %p unreadable", i, I);
			continue;
		}
		u8			text[64];
		bool		ok		= name && lanim_read(name, text, 1);
		u32			len		= 0;
		if (ok)
		{
			for (; len < sizeof(text); ++len)
			{
				if (!lanim_read(name + len, text + len, 1))	{ ok = false; break; }
				if (!text[len])								break;
				if (text[len] < 32 || text[len] >= 127)		{ ok = false; break; }
			}
			ok		= ok && len > 0 && len < sizeof(text);
		}
		if (ok)
		{
			++sane;
			continue;
		}
		Msg("! [lanim]   record %u: item %p, name %p is not a valid string", i, I, name);
		lanim_hex("item  ", I, 32);
		lanim_hex("string", name ? name - 16 : NULL, 48);
	}
	Msg("! [lanim]   %u of %u records have a valid name, none of them [%s]", sane, cnt, wanted);
	FlushLog();
}

//////////////////////////////////////////////////////////////////////////

CUIColorAnimatorWrapper::CUIColorAnimatorWrapper()
	:	colorAnimation		(NULL),
		animationTime		(0),
		color				(NULL),
		isDone				(false),
		reverse				(false),
		kRev				(0.0f)
{
	prevGlobalTime	= Device.dwTimeContinual/1000.0f;
	currColor		= 0xffff0000;
}

CUIColorAnimatorWrapper::CUIColorAnimatorWrapper(u32 *colorToModify)
	:	colorAnimation		(NULL),
	animationTime		(0),
	color				(NULL),
	isDone				(false),
	reverse				(false),
	kRev				(0.0f)
{
	prevGlobalTime	= Device.dwTimeContinual/1000.0f;
	color			= colorToModify;
	currColor		= 0xffff0000;
}
//////////////////////////////////////////////////////////////////////////

CUIColorAnimatorWrapper::CUIColorAnimatorWrapper(const shared_str &animationName)
	:	colorAnimation		(LALib.FindItem(*animationName)),
		animationTime		(0),
		color				(NULL),
		isDone				(false),
		reverse				(false),
		kRev				(0.0f)
{
	VERIFY(colorAnimation);
	prevGlobalTime	= Device.dwTimeContinual/1000.0f;
	currColor		= 0xffff0000;
}

void CUIColorAnimatorWrapper::SetColorAnimation(const shared_str &animationName)
{
	if (animationName.size() != 0)
	{
		colorAnimation	= LALib.FindItem(*animationName);
		if (!colorAnimation)
			lanim_report_miss(*animationName);	// was R_ASSERT2: see lanim_report_miss
	}
	else
	{
		colorAnimation	= NULL;
	}
}

//////////////////////////////////////////////////////////////////////////

void CUIColorAnimatorWrapper::SetColorToModify(u32 *colorToModify)
{
	color = colorToModify;
}

//////////////////////////////////////////////////////////////////////////

void CUIColorAnimatorWrapper::Update()
{
	if (colorAnimation && !isDone)
	{
		if (!isCyclic)
		{
			if (animationTime < (colorAnimation->iFrameCount / colorAnimation->fFPS))
			{
				currColor		= colorAnimation->CalculateBGR(std::abs(animationTime - kRev), currFrame);
				//Msg("name: %s, color: %x, frame: %d", *colorAnimation->cName,currColor, currFrame);
				currColor		= color_rgba(color_get_B(currColor), color_get_G(currColor), color_get_R(currColor), color_get_A(currColor));
				// обновим время
				animationTime	+= Device.dwTimeContinual/1000.0f - prevGlobalTime;
			}
			else
			{
				// В любом случае (при любом ФПС) последним кадром должен быть последний кадр анимации
				currColor	= colorAnimation->CalculateBGR((colorAnimation->iFrameCount - 1) / colorAnimation->fFPS - kRev, currFrame);
				currColor	= color_rgba(color_get_B(currColor), color_get_G(currColor), color_get_R(currColor), color_get_A(currColor));
				// Индицируем конец анимации
				isDone = true;
			}
		}
		else
		{
			currColor	= colorAnimation->CalculateBGR(Device.dwTimeContinual/1000.0f, currFrame);
			currColor	= color_rgba(color_get_B(currColor), color_get_G(currColor), color_get_R(currColor), color_get_A(currColor));
		}

		if (color)
		{
			*color		= currColor;
		}
	}

	prevGlobalTime = Device.dwTimeContinual/1000.0f;
}

//////////////////////////////////////////////////////////////////////////

void CUIColorAnimatorWrapper::Reset()
{
	prevGlobalTime	= Device.dwTimeContinual/1000.0f;
	animationTime	= 0;
	isDone			= false;
}

//////////////////////////////////////////////////////////////////////////

int CUIColorAnimatorWrapper::TotalFrames() const
{
	if (colorAnimation)
	{
		return colorAnimation->iFrameCount;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////

void CUIColorAnimatorWrapper::Reverese(bool value)
{
	reverse = value;

	if (value)
	{
		kRev = (colorAnimation->iFrameCount - 1) / colorAnimation->fFPS;
	}
	else
	{
		kRev = 0.0f;
	}

	if (!Done())
	{
		animationTime = colorAnimation->iFrameCount / colorAnimation->fFPS - animationTime;
	}
}

void CUIColorAnimatorWrapper::GoToEnd(){
	prevGlobalTime	= Device.dwTimeContinual/1000.0f;
	this->currFrame = colorAnimation->iFrameCount;
	animationTime = colorAnimation->iFrameCount / colorAnimation->fFPS;
	this->isDone = false;
}