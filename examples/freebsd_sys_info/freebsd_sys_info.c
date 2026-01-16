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

#include <err.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <devstat.h>
#include <libgeom.h>
#include <stdio.h>
#include <sys/sysctl.h>

#include "perfetto/public/data_source.h"
#include "perfetto/public/producer.h"
#include "perfetto/public/protos/trace/system_info/cpu_info.pzc.h"

extern void setup_cpu_info_data(void);
extern void fetch_cpu_info_data(struct perfetto_protos_CpuInfo* sys_stat);

static struct PerfettoDs custom = PERFETTO_DS_INIT();

static uint64_t get_current_time_ns(void) {
  struct timespec ts;

  (void)clock_gettime(CLOCK_BOOTTIME, &ts);

  return (((uint64_t)ts.tv_sec * 1000000000ULL) + ts.tv_nsec);
}

static void cpu_info_on_start_cb_flush_cb(void* arg) {
  (void)arg;
  printf("%s: called!\n", __func__);
}

static void cpu_info_on_start_cb(struct PerfettoDsImpl* impl,
                                 PerfettoDsInstanceIndex inst_id,
                                 void* user_arg,
                                 void* inst_ctx,
                                 struct PerfettoDsOnStartArgs* args) {
  (void)impl;
  (void)inst_id;
  (void)user_arg;
  (void)inst_ctx;
  (void)args;

  printf("%s: called!;  inst=%d arg=%p ctx=%p onstartargs=%p\n", __func__,
         inst_id, user_arg, inst_ctx, (void*)args);

  PERFETTO_DS_TRACE(custom, ctx) {
    struct PerfettoDsRootTracePacket root;
    struct perfetto_protos_CpuInfo cpu_info;

    PerfettoDsTracerPacketBegin(&ctx, &root);
    perfetto_protos_TracePacket_set_timestamp(&root.msg, get_current_time_ns());

    perfetto_protos_TracePacket_begin_cpu_info(&root.msg, &cpu_info);
    fetch_cpu_info_data(&cpu_info);
    perfetto_protos_TracePacket_end_cpu_info(&root.msg, &cpu_info);
    PerfettoDsTracerPacketEnd(&ctx, &root);

    PerfettoDsTracerFlush(&ctx, cpu_info_on_start_cb_flush_cb, NULL);
  }

#if 0
	void *iterator;
	iterator = PerfettoDsImplGetInstanceLocked(impl, inst_id);

	// For some reason iterator is null here? what's going on */

	if (iterator == NULL) {
		printf("%s: iterator is null!\n", __func__);
	} else {
		PerfettoDsTracerFlush(iterator, cpu_info_on_start_cb_flush_cb, NULL);
		PerfettoDsImplReleaseInstanceLocked(impl, inst_id);
	}
#endif
}

int main(void) {
  struct PerfettoProducerInitArgs args = PERFETTO_PRODUCER_INIT_ARGS_INIT();
  struct PerfettoDsParams ds_params;
  args.backends = PERFETTO_BACKEND_SYSTEM;
  PerfettoProducerInit(args);

  // This ends up calling PerfettoProducerSystemInit() is called
  // At this point the perfetto::Tracing::Initialize() is called
  // Ok, next, what's PerfettoDsRegister()ing against

  /* Initialise the default parameters */
  ds_params = PerfettoDsParamsDefault();

  /* Add an on start method for generating the message */
  ds_params.on_start_cb = cpu_info_on_start_cb;

  PerfettoDsRegister(&custom, "freebsd.sys_info", ds_params);

  for (;;) {
    sleep(5);
  }
}
