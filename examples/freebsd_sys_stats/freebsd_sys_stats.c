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

#include <stdio.h>
#include <libgeom.h>
#include <devstat.h>

#include "perfetto/public/data_source.h"
#include "perfetto/public/producer.h"
#include "perfetto/public/protos/trace/sys_stats/sys_stats.pzc.h"
#include "perfetto/public/protos/trace/trace_packet.pzc.h"

struct {
    struct gmesh gmp;
    struct devstat *gsp, *gsq;
    void *sp, *sq;
    struct timespec tp, tq;
    float dt;
} geom_info;

static struct PerfettoDs custom = PERFETTO_DS_INIT();

static uint64_t
get_current_time_ns(void)
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_BOOTTIME, &ts);

	return (((uint64_t) ts.tv_sec * 1000000000ULL) + ts.tv_nsec);
}

static void populate_disk_data(struct perfetto_protos_SysStats *sys_stat)
{
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

        printf("disk: %s, sect %lu/%lu, tot %lu/%lu\n", devname, tr_rx, tr_wr, by_rx, by_wr);

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
}

int main(void) {
  struct PerfettoProducerInitArgs args = PERFETTO_PRODUCER_INIT_ARGS_INIT();
  args.backends = PERFETTO_BACKEND_SYSTEM;
  PerfettoProducerInit(args);

  /* GEOM for disk stats */
  geom_gettree(&geom_info.gmp);
  geom_stats_open();

  /* Get initial disk snapshot */
  geom_info.sq = geom_stats_snapshot_get();
  geom_stats_snapshot_timestamp(geom_info.sq, &geom_info.tq);

  PerfettoDsRegister(&custom, "freebsd.sys_stats", PerfettoDsParamsDefault());

  for (;;) {
    PERFETTO_DS_TRACE(custom, ctx) {
      struct PerfettoDsRootTracePacket root;
      PerfettoDsTracerPacketBegin(&ctx, &root);

      perfetto_protos_TracePacket_set_timestamp(&root.msg, get_current_time_ns());
      {
        struct perfetto_protos_SysStats sys_stats;

        perfetto_protos_TracePacket_begin_sys_stats(&root.msg, &sys_stats);

        populate_disk_data(&sys_stats);

        perfetto_protos_TracePacket_end_sys_stats(&root.msg, &sys_stats);

#if 0
        struct perfetto_protos_TestEvent for_testing;
        perfetto_protos_TracePacket_begin_for_testing(&root.msg, &for_testing);

        perfetto_protos_TestEvent_set_cstr_str(&for_testing,
                                               "This is a long string");
        {
          struct perfetto_protos_TestEvent_TestPayload payload;
          perfetto_protos_TestEvent_begin_payload(&for_testing, &payload);

          for (int i = 0; i < 1000; i++) {
            perfetto_protos_TestEvent_TestPayload_set_cstr_str(&payload,
                                                               "nested");
          }
          perfetto_protos_TestEvent_end_payload(&for_testing, &payload);
        }
        perfetto_protos_TracePacket_end_for_testing(&root.msg, &for_testing);
#endif
      }
      PerfettoDsTracerPacketEnd(&ctx, &root);
    }
    // 100ms sleep
    usleep(100 * 1000);
    //sleep(1);
  }
}
