#include "stdafx.h"
#pragma hdrstop

#include "hitch_trace.h"
#include <psapi.h>

namespace hitch
{
	u32		threshold_ms	= 0;

	namespace
	{
		struct zone_rec	{ LPCSTR name; u64 ticks; u32 count; };
		struct kind_rec	{ LPCSTR kind; u64 ticks; u32 count; };
		struct load_rec	{ LPCSTR kind; string128 what; u64 ticks; };

		const u32	max_zones	= 128;
		const u32	max_kinds	= 32;
		const u32	max_loads	= 256;

		zone_rec	s_zones	[max_zones];	u32 s_zone_count	= 0;
		kind_rec	s_kinds	[max_kinds];	u32 s_kind_count	= 0;
		load_rec	s_loads	[max_loads];	u32 s_load_count	= 0;
		u32			s_loads_dropped		= 0;

		// Static init without a constructor: zones may run before xrCore is up.
		SRWLOCK		s_lock				= SRWLOCK_INIT;

		struct guard
		{
			guard	()	{ AcquireSRWLockExclusive(&s_lock); }
			~guard	()	{ ReleaseSRWLockExclusive(&s_lock); }
		};

		float	to_ms	(u64 ticks)	{ return CPU::qpc_freq ? float(double(ticks) * 1000.0 / double(CPU::qpc_freq)) : 0.f; }

		// Process state, sampled every frame so a report can show what changed during the slow one: did the
		// main thread actually run (its cycles) or wait, did the whole process run (CPU time), and did it
		// fault pages in (a frame where everything is slow at once looks like paging, not like any one system).
		struct proc_sample
		{
			u64		thread_cycles;		// main thread
			u64		process_100ns;		// user + kernel, all threads
			u32		page_faults;
			u32		working_set_mb;
			u32		commit_mb;
		};

		typedef BOOL (WINAPI *get_memory_info_fn)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);

		proc_sample	sample_process()
		{
			static get_memory_info_fn	s_get_memory_info	= (get_memory_info_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
			proc_sample	s;
			ZeroMemory	(&s, sizeof(s));
			ULONG64		cycles	= 0;
			QueryThreadCycleTime(GetCurrentThread(), &cycles);
			s.thread_cycles		= cycles;
			FILETIME	c, e, k, u;
			if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
				s.process_100ns	= (u64(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (u64(u.dwHighDateTime) << 32 | u.dwLowDateTime);
			PROCESS_MEMORY_COUNTERS	pmc;
			ZeroMemory	(&pmc, sizeof(pmc));
			pmc.cb		= sizeof(pmc);
			if (s_get_memory_info && s_get_memory_info(GetCurrentProcess(), &pmc, sizeof(pmc)))
			{
				s.page_faults		= pmc.PageFaultCount;
				s.working_set_mb	= u32(pmc.WorkingSetSize >> 20);
				s.commit_mb			= u32(pmc.PagefileUsage >> 20);
			}
			return		s;
		}

		proc_sample	s_prev_sample;
		bool		s_have_prev	= false;
		u64			s_start_qpc	= 0;
	}

	void zone_add(LPCSTR name, u64 ticks)
	{
		guard	g;
		for (u32 i = 0; i < s_zone_count; ++i)
			if (s_zones[i].name == name)	{ s_zones[i].ticks += ticks; ++s_zones[i].count; return; }
		if (s_zone_count == max_zones)	return;
		zone_rec& z		= s_zones[s_zone_count++];
		z.name			= name;
		z.ticks			= ticks;
		z.count			= 1;
	}

	void load_add(LPCSTR kind, LPCSTR what, u64 ticks)
	{
		guard	g;
		u32 k = 0;
		for (; k < s_kind_count; ++k)
			if (s_kinds[k].kind == kind)	break;
		if (k == s_kind_count && s_kind_count < max_kinds)
		{
			s_kinds[k].kind		= kind;
			s_kinds[k].ticks	= 0;
			s_kinds[k].count	= 0;
			++s_kind_count;
		}
		if (k < s_kind_count)	{ s_kinds[k].ticks += ticks; ++s_kinds[k].count; }

		// Only the ones worth a line: the rest still count in the per-kind totals.
		if (ticks * 4000 < CPU::qpc_freq)	return;		// < 0.25 ms
		if (s_load_count == max_loads)		{ ++s_loads_dropped; return; }
		load_rec& l		= s_loads[s_load_count++];
		l.kind			= kind;
		l.ticks			= ticks;
		strncpy_s		(l.what, sizeof(l.what), what, _TRUNCATE);
	}

	void item_end(LPCSTR kind, u64 t0, LPCSTR name, LPCSTR detail)
	{
		if (!t0)	return;
		const u64 dt	= CPU::QPC() - t0;
		if (dt * 1000 < CPU::qpc_freq)	return;		// < 1 ms
		string128	what;
		// Truncating: the _s formatters would abort the game on a long name.
		if (detail)	_snprintf_s(what, sizeof(what), _TRUNCATE, "%s [%s]", name ? name : "?", detail);
		else		_snprintf_s(what, sizeof(what), _TRUNCATE, "%s", name ? name : "?");
		load_add	(kind, what, dt);
	}

	void frame_end(u32 frame, u64 frame_ticks, bool report)
	{
		guard	g;
		const float frame_ms	= to_ms(frame_ticks);
		const proc_sample now	= threshold_ms ? sample_process() : proc_sample();
		if (!s_start_qpc)		s_start_qpc = CPU::QPC();
		if (report && threshold_ms && frame_ms >= float(threshold_ms))
		{
			Msg	("~ [hitch] frame %u: %.1f ms (threshold %u ms), t=%.1f s", frame, frame_ms, threshold_ms, to_ms(CPU::QPC() - s_start_qpc) / 1000.f);
			if (s_have_prev)
			{
				const float main_ms	= CPU::clk_per_milisec ? float(double(now.thread_cycles - s_prev_sample.thread_cycles) / double(CPU::clk_per_milisec)) : 0.f;
				const float proc_ms	= float(double(now.process_100ns - s_prev_sample.process_100ns) / 10000.0);
				Msg	("~ [hitch]   process: main thread busy ~%.0f ms, all threads %.0f ms CPU, page faults +%u, working set %u MB, commit %u MB",
					main_ms, proc_ms, now.page_faults - s_prev_sample.page_faults, now.working_set_mb, now.commit_mb);
			}

			std::sort(s_zones, s_zones + s_zone_count, [](const zone_rec& a, const zone_rec& b) { return a.ticks > b.ticks; });
			for (u32 i = 0; i < s_zone_count && i < 24; ++i)
			{
				const float ms	= to_ms(s_zones[i].ticks);
				if (ms < 1.f)	break;
				Msg	("~ [hitch]   zone %7.1f ms  %s (x%u)", ms, s_zones[i].name, s_zones[i].count);
			}

			if (s_kind_count)
			{
				string1024	line	= "";
				for (u32 k = 0; k < s_kind_count; ++k)
				{
					string64	one;
					_snprintf_s	(one, sizeof(one), _TRUNCATE, "%s%s %u/%.1f ms", k ? ", " : "", s_kinds[k].kind, s_kinds[k].count, to_ms(s_kinds[k].ticks));
					strncat_s	(line, sizeof(line), one, _TRUNCATE);
				}
				Msg	("~ [hitch]   loads: %s", line);
			}

			std::sort(s_loads, s_loads + s_load_count, [](const load_rec& a, const load_rec& b) { return a.ticks > b.ticks; });
			for (u32 i = 0; i < s_load_count && i < 24; ++i)
				Msg	("~ [hitch]   load %7.1f ms  %s %s", to_ms(s_loads[i].ticks), s_loads[i].kind, s_loads[i].what);
			if (s_loads_dropped)
				Msg	("~ [hitch]   (+%u loads over the record limit)", s_loads_dropped);
		}

		s_prev_sample	= now;
		s_have_prev		= threshold_ms != 0;
		s_zone_count	= 0;
		s_kind_count	= 0;
		s_load_count	= 0;
		s_loads_dropped	= 0;
	}
}
