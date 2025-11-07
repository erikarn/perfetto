/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unistd.h>
#include <time.h>
#include <err.h>
#include <errno.h>

#include <stdio.h>
#include <libgeom.h>
#include <devstat.h>
#include <sys/sysctl.h>
#include <sys/resource.h>

#include "perfetto/public/data_source.h"
#include "perfetto/public/producer.h"
#include "perfetto/public/protos/trace/sys_stats/sys_stats.pzc.h"
#include "perfetto/public/protos/trace/trace_packet.pzc.h"

struct {
	int maxcpu;
	long *times;
	long *last_cp_times;
	int times_size;
	u_long mask;
	int maxid;
	bool have_prev;
} cpu_info;

void
setup_cpu_data(void)
{
	size_t size;
	int ret;
	int empty, i, j;

	/* Figure out the maximum CPU id */
	size = sizeof(int);
	ret = sysctlbyname("kern.smp.maxcpus", &cpu_info.maxcpu, &size, NULL, 0);
	if (ret != 0) {
		printf("%s: kern.smp.maxcpus failed (%d)\n", __func__, ret);
		return;
	}

	printf("maxcpu: %d\n", cpu_info.maxcpu);

	/* Allocate cpu time array for current and previous cpu times */
	cpu_info.times_size = sizeof(long) * cpu_info.maxcpu * CPUSTATES;
	cpu_info.times = calloc(1, cpu_info.times_size);
	cpu_info.last_cp_times = calloc(1, cpu_info.times_size);

	/* Fetch a round of CPU stats, likely to figure out what's up */
	size = cpu_info.times_size;
	ret = sysctlbyname("kern.cp_times", cpu_info.times, &size, NULL, 0);
	if (ret != 0) {
		printf("%s: kern.cp_times failed (%d)\n", __func__, errno);
		return;
	}

	/* figure out the maximum id */
	cpu_info.maxid = (size / CPUSTATES / sizeof(long)) - 1;

	for (i = 0; i <= cpu_info.maxid; i++) {
		empty = 1;
		for (j = 0; empty && j < CPUSTATES; j++) {
			if (cpu_info.times[i * CPUSTATES + j] != 0)
				empty = 0;
		}
		if (!empty)
			cpu_info.mask |= (1ul << i);
	}
}

static unsigned long
fetch_cp_time_delta(int cpu, int which)
{
	unsigned long delta;

	delta = cpu_info.times[cpu * CPUSTATES + which] -
	    cpu_info.last_cp_times[cpu * CPUSTATES + which];

	delta = (delta * 1000000000ULL) / CLOCKS_PER_SEC;

	return (delta);
}

void
populate_cpu_data(struct perfetto_protos_SysStats *sys_stat)
{
	(void) sys_stat;
	size_t size;
	int ret, i;

	size = cpu_info.times_size;
	ret = sysctlbyname("kern.cp_times", cpu_info.times, &size, NULL, 0);
	if (ret != 0) {
		printf("%s: kern.cp_times failed (%d)\n", __func__, errno);
		return;
	}

	if (cpu_info.have_prev == false)
		goto skip;

	for (i = 0; i <= cpu_info.maxid; i++) {
		struct perfetto_protos_SysStats_CpuTimes cpu_cnt;
		unsigned long delta;

		if ((cpu_info.mask & (1ul << i)) == 0)
			continue;

		perfetto_protos_SysStats_begin_cpu_stat(sys_stat, &cpu_cnt);

		/* cpu_id */
		perfetto_protos_SysStats_CpuTimes_set_cpu_id(&cpu_cnt, i);

#if 0
		printf("cpu %d: idle=%ld, user=%ld, nice=%ld, system=%ld, intr=%ld\n",
		    i,
		    fetch_cp_time_delta(i, CP_IDLE),
		    fetch_cp_time_delta(i, CP_USER),
		    fetch_cp_time_delta(i, CP_NICE),
		    fetch_cp_time_delta(i, CP_SYS),
		    fetch_cp_time_delta(i, CP_INTR));
#endif

		/*
		 * TODO: are these in units of CLOCKS_PER_SEC (128Hz) ?
		 * How do I convert these to nanosecond values?
		 */

		/* user_ns */
		delta = fetch_cp_time_delta(i, CP_USER);
		perfetto_protos_SysStats_CpuTimes_set_user_ns(&cpu_cnt, delta);

		/* user_nice_ns */
		delta = fetch_cp_time_delta(i, CP_NICE);
		perfetto_protos_SysStats_CpuTimes_set_user_nice_ns(&cpu_cnt, delta);

		/* system_mode_ns */
		delta = fetch_cp_time_delta(i, CP_SYS);
		perfetto_protos_SysStats_CpuTimes_set_system_mode_ns(&cpu_cnt, delta);

		/* idle_ns */
		delta = fetch_cp_time_delta(i, CP_IDLE);
		perfetto_protos_SysStats_CpuTimes_set_idle_ns(&cpu_cnt, delta);

		/* io_wait_ns */

		/* irq_ns */
		delta = fetch_cp_time_delta(i, CP_INTR);
		perfetto_protos_SysStats_CpuTimes_set_irq_ns(&cpu_cnt, delta);

		/* softirq_ns */
		/* steal_ns */

		perfetto_protos_SysStats_end_cpu_stat(sys_stat, &cpu_cnt);
	}

skip:
	memcpy(cpu_info.last_cp_times, cpu_info.times, cpu_info.times_size);
	cpu_info.have_prev = true;
}


