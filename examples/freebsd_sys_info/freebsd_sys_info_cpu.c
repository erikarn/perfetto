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
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <devstat.h>
#include <libgeom.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/sysctl.h>

#include "perfetto/public/data_source.h"
#include "perfetto/public/producer.h"
#include "perfetto/public/protos/trace/system_info/cpu_info.pzc.h"

/*
 * TODO: we only support 64 CPUs here (u_long mask)!
 *
 * TODO: holy crap surely there's a better way to get the active CPU set mask!
 */

struct {
  int maxcpu;
  u_long mask;
  int maxid;
} cpu_info;

void setup_cpu_info_data(void) {
  size_t size;
  int ret;
  int empty, i, j;
  long* times;
  size_t times_size;

  /* Figure out the maximum CPU id */
  size = sizeof(int);
  ret = sysctlbyname("kern.smp.maxcpus", &cpu_info.maxcpu, &size, NULL, 0);
  if (ret != 0) {
    printf("%s: kern.smp.maxcpus failed (%d)\n", __func__, ret);
    return;
  }

  printf("maxcpu: %d\n", cpu_info.maxcpu);

  /* Allocate cpu time array for current and previous cpu times */
  times_size = sizeof(long) * cpu_info.maxcpu * CPUSTATES;
  times = calloc(1, times_size);

  /* Fetch a round of CPU stats, likely to figure out what's up */
  size = times_size;
  ret = sysctlbyname("kern.cp_times", times, &size, NULL, 0);
  if (ret != 0) {
    printf("%s: kern.cp_times failed (%d)\n", __func__, errno);
    free(times);
    return;
  }

  /* figure out the maximum id */
  cpu_info.maxid = (size / CPUSTATES / sizeof(long)) - 1;

  for (i = 0; i <= cpu_info.maxid; i++) {
    empty = 1;
    for (j = 0; empty && j < CPUSTATES; j++) {
      if (times[i * CPUSTATES + j] != 0)
        empty = 0;
    }
    if (!empty)
      cpu_info.mask |= (1ul << i);
  }
  free(times);
}

void fetch_cpu_info_data(struct perfetto_protos_CpuInfo* cpus) {
  int i;

  for (i = 0; i <= cpu_info.maxid; i++) {
    struct perfetto_protos_CpuInfo_Cpu cpu;
    const char* cpustr = "Intel CPU";

    perfetto_protos_CpuInfo_begin_cpus(cpus, &cpu);

    /* XXX - sysctl hw.model; but that's only one for all cores! */
    perfetto_protos_CpuInfo_Cpu_set_processor(&cpu, cpustr, strlen(cpustr));
    /* TODO: push list of frequencies in - sysctl dev.cpu.X.freq_levels */
    perfetto_protos_CpuInfo_end_cpus(cpus, &cpu);
  }
}
