#pragma once

#include "SchedEntity.h"

/* include/linux/sched.h */
constexpr static int nice_to_weigh[] = {
	/* -20 */     88761,     71755,     56483,     46273,     36291,
	/* -15 */     29154,     23254,     18705,     14949,     11916,
	/* -10 */     9548,     7620,     6100,     4904,     3906,
	/*  -5 */     3121,     2501,     1991,     1586,     1277,
	/*   0 */     1024,     820,      655,      526,      423,
	/*   5 */     335,      272,      215,      172,      137,
	/*  10 */     110,      87,       70,       56,       45,
	/*  15 */     36,       29,       23,       18,       15,
};

class CfsSchedEntity : public SchedEntity
{
public:
	constexpr static int nice_offset = 20;

	uint16_t nice{};
	uint64_t v_runtime{};

	[[nodiscard]] uint64_t priority() const { return v_runtime; }

	void prefetch_v_runtime() const { __builtin_prefetch(&v_runtime, 0, 3); }

	void up_v_runtime()
	{
		up_real_runtime();
		v_runtime += (real_runtime << 10) / nice_to_weigh[nice + nice_offset];
	}
};