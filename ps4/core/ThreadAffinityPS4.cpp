// SPDX-License-Identifier: GPL-3.0+

#include "ThreadAffinityPS4.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

// OpenOrbis's public header declares these without parameters. Use the ABI
// verified by DolphinPS4 until the SDK declaration is corrected.
extern "C" void* scePthreadSelf();
extern "C" int scePthreadSetaffinity(void* thread, unsigned long long mask);
extern "C" int scePthreadSetprio(void* thread, int prio);

namespace PS4ThreadAffinity
{
	// (AI-assisted) Optional thread priority boost, off unless /data/PS4SX2/vm-thread-priority
	// exists. Deliberately opt-in: the port already pins the hot threads to dedicated cores
	// (EE->2, VU1->3, Audio->5), and on a dedicated core a priority changes nothing. Priorities
	// only matter once vm-no-thread-affinity hands placement back to the system scheduler, so
	// this is a companion knob rather than a default. On Orbis, 0 is the highest priority and
	// 767 the lowest; game threads normally run near 700.
	static bool PriorityBoostEnabled()
	{
		static const bool enabled = access("/data/PS4SX2/vm-thread-priority", F_OK) == 0;
		return enabled;
	}

	static int PriorityForRole(const char* role)
	{
		// The EE is the bottleneck thread on this hardware, so it leads; VU1 runs its own
		// thread under MTVU, and audio must stay responsive without outranking either.
		if (std::strcmp(role, "EE") == 0)
			return 300;
		if (std::strcmp(role, "VU1") == 0)
			return 350;
		if (std::strcmp(role, "Audio") == 0)
			return 500;
		return 400;
	}

	static void ApplyPriority(const char* role)
	{
		if (!PriorityBoostEnabled())
			return;

		const int prio = PriorityForRole(role);
		const int result = scePthreadSetprio(scePthreadSelf(), prio);
		std::printf("PS4 thread priority: %s prio=%d sce_result=0x%x\n", role, prio, result);
		std::fflush(stdout);
	}

	static bool IsEnabled()
	{
		static std::atomic<int> enabled{-1};
		int value = enabled.load(std::memory_order_acquire);
		if (value >= 0)
			return value != 0;

		value = access("/data/PS4SX2/vm-no-thread-affinity", F_OK) == 0 ? 0 : 1;
		enabled.store(value, std::memory_order_release);
		return value != 0;
	}

	void PinCurrentThread(const char* role, int core)
	{
		// (AI-assisted) Optional priority boost, independent of the affinity decision - it is
		// the companion optimisation for when placement is handed back to the scheduler.
		ApplyPriority(role);

		if (!IsEnabled())
		{
			std::printf("PS4 thread affinity: %s left to scheduler (disabled by marker)\n", role);
			std::fflush(stdout);
			return;
		}

		const unsigned long long mask = 1ULL << core;
		const int result = scePthreadSetaffinity(scePthreadSelf(), mask);
		// (AI-assisted) Core placement needs proper testing across base/Pro consoles.
		std::printf("PS4 thread affinity: %s core=%d mask=0x%llx sce_result=0x%x\n", role, core, mask, result);
		std::fflush(stdout);
	}
} // namespace PS4ThreadAffinity
