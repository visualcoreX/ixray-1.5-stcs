#pragma once

// Frame hitch tracer. A freeze while running across a level cannot be reproduced on demand, so the
// frame that froze has to explain itself: zones add up time per frame under static names, loads
// record one entry per expensive resource load (texture, model, sound, shader, spawn ...) with what
// was loaded. When a frame runs longer than the threshold the device prints both, longest first;
// every frame starts from zero. A scope costs two QPC reads and a short uncontended lock.
namespace hitch
{
	XRCORE_API extern u32	threshold_ms;		// 0 = off

	// name / kind must be static strings (they are kept as pointers); what is copied.
	XRCORE_API void			zone_add	(LPCSTR name, u64 ticks);
	XRCORE_API void			load_add	(LPCSTR kind, LPCSTR what, u64 ticks);

	// For loops over many items (object updates, scheduler): t0 = item_begin() before the item,
	// item_end() after it records the item as a load only when it took a millisecond or more, so
	// the cheap common case never formats a name.
	IC u64					item_begin	()	{ return threshold_ms ? CPU::QPC() : 0; }
	XRCORE_API void			item_end	(LPCSTR kind, u64 t0, LPCSTR name, LPCSTR detail = 0);

	// Called once per frame by the device. report=false just resets (loading screen, pause, menu).
	XRCORE_API void			frame_end	(u32 frame, u64 frame_ticks, bool report);

	class zone
	{
		LPCSTR		m_name;
		u64			m_start;
	public:
		zone	(LPCSTR name) : m_name(name), m_start(threshold_ms ? CPU::QPC() : 0)	{}
		~zone	()	{ if (m_start) zone_add(m_name, CPU::QPC() - m_start); }
	};

	class load
	{
		LPCSTR		m_kind;
		string128	m_what;
		u64			m_start;
	public:
		load	(LPCSTR kind, LPCSTR what) : m_kind(kind), m_start(0)
		{
			if (!threshold_ms)	return;
			strncpy_s	(m_what, sizeof(m_what), what ? what : "?", _TRUNCATE);
			m_start		= CPU::QPC();
		}
		~load	()	{ if (m_start) load_add(m_kind, m_what, CPU::QPC() - m_start); }
	};
}
