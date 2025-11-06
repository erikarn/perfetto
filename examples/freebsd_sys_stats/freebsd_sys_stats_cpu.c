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
		//printf("cpu: %d\n", i);

		/*
		 * TODO: these are actually values between 0..100; we'll need
		 * to convert it to nanoseconds based on the polling interval.
		 */

		delta = cpu_info.times[i * CPUSTATES + CP_USER] -
		     cpu_info.last_cp_times[i * CPUSTATES + CP_USER];

		/* user_ns */
		perfetto_protos_SysStats_CpuTimes_set_user_ns(&cpu_cnt, delta);

		/* user_nice_ns */
		delta = cpu_info.times[i * CPUSTATES + CP_NICE] -
		     cpu_info.last_cp_times[i * CPUSTATES + CP_NICE];
		perfetto_protos_SysStats_CpuTimes_set_user_nice_ns(&cpu_cnt, delta);

		/* system_mode_ns */
		delta = cpu_info.times[i * CPUSTATES + CP_SYS] -
		     cpu_info.last_cp_times[i * CPUSTATES + CP_SYS];
		perfetto_protos_SysStats_CpuTimes_set_system_mode_ns(&cpu_cnt, delta);

		/* idle_ns */
		delta = cpu_info.times[i * CPUSTATES + CP_IDLE] -
		     cpu_info.last_cp_times[i * CPUSTATES + CP_IDLE];
		perfetto_protos_SysStats_CpuTimes_set_idle_ns(&cpu_cnt, delta);

		/* io_wait_ns */

		/* irq_ns */
		delta = cpu_info.times[i * CPUSTATES + CP_INTR] -
		     cpu_info.last_cp_times[i * CPUSTATES + CP_INTR];
		perfetto_protos_SysStats_CpuTimes_set_irq_ns(&cpu_cnt, delta);

		/* softirq_ns */
		/* steal_ns */


		perfetto_protos_SysStats_end_cpu_stat(sys_stat, &cpu_cnt);
	}

skip:
	memcpy(cpu_info.last_cp_times, cpu_info.times, cpu_info.times_size);
	cpu_info.have_prev = true;
#if 0
    struct gident *gid;
    char devname[4096];
    uint64_t q_len;
    uint64_t tr_rx, tr_wr, by_rx, by_wr;

    geom_info.sp = geom_stats_snapshot_get();
    if (geom_info.sp == NULL) /* XXX error */
        return;
    geom_stats_snapshot_timestamp(geom_info.sp, &geom_info.tp);

    geom_info.dt = geom_info.tp.tv_sec - geom_info.tq.tv_sec;
    geom_info.dt += (geom_info.tp.tv_nsec - geom_info.tq.tv_nsec) * 1e-9;
    geom_info.tq = geom_info.tp;

    geom_stats_snapshot_reset(geom_info.sp);
    geom_stats_snapshot_reset(geom_info.sq);

    for (;;) {
        struct perfetto_protos_SysStats_DiskStat disk_stats;

        geom_info.gsp = geom_stats_snapshot_next(geom_info.sp);
        geom_info.gsq = geom_stats_snapshot_next(geom_info.sq);

        if (geom_info.gsp == NULL || geom_info.gsq == NULL) {
                break;
        }
        if (geom_info.gsp->id == NULL) {
                continue;
        }

        /* gstat will delete/reload the tree; here for now just skip */
        gid = geom_lookupid(&geom_info.gmp, geom_info.gsp->id);
        if (gid == NULL) {
                continue;
        }

        /* only consumers */
        if (gid->lg_what != ISPROVIDER) {
                continue;
        }
        /* only physical devices for now */
        /* note: this EXPECTS it's a provider! */
        if (((struct gprovider *)(gid->lg_ptr))->lg_geom->lg_rank != 1) {
                continue;
        }
        /* populate the name - again expects its a provider */
        snprintf(devname, sizeof(devname), "%s", ((struct gprovider *)(gid->lg_ptr))->lg_name);

        /* calculate statistics over the interval */
        devstat_compute_statistics(geom_info.gsp, NULL, geom_info.dt,
            DSM_QUEUE_LENGTH, &q_len,
            DSM_TOTAL_TRANSFERS_READ, &tr_rx,
            DSM_TOTAL_BYTES_READ, &by_rx,
            DSM_TOTAL_TRANSFERS_WRITE, &tr_wr,
            DSM_TOTAL_BYTES_WRITE, &by_wr,
            DSM_NONE);

        //printf("disk: %s, sect %lu/%lu, tot %lu/%lu\n", devname, tr_rx, tr_wr, by_rx, by_wr);

        /* Populate a disk stat entry */
        perfetto_protos_SysStats_begin_disk_stat(sys_stat, &disk_stats);

        perfetto_protos_SysStats_DiskStat_set_device_name(&disk_stats, devname, strlen(devname));
        perfetto_protos_SysStats_DiskStat_set_read_sectors(&disk_stats, tr_rx);
        /* todo: read_time_ms */
        perfetto_protos_SysStats_DiskStat_set_write_sectors(&disk_stats, tr_wr);
        /* todo: write_time_ms */
        /* todo: discard_sectors */
        /* todo: discard_time_ms */
        /* todo: flush_count */
        /* todo: flush_time_ms */

        perfetto_protos_SysStats_end_disk_stat(sys_stat, &disk_stats);

        *geom_info.gsq = *geom_info.gsp;
        geom_stats_snapshot_free(geom_info.sp);
    }
#endif
}


