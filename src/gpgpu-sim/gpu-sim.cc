// Copyright (c) 2009-2011, Tor M. Aamodt, Wilson W.L. Fung, George L. Yuan,
// Ali Bakhoda, Andrew Turner, Ivan Sham
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#include "gpu-sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "zlib.h"


#include "shader.h"
#include "dram.h"
#include "mem_fetch.h"

#include <time.h>
#include "gpu-cache.h"
#include "gpu-misc.h"
#include "delayqueue.h"
#include "shader.h"
#include "icnt_wrapper.h"
#include "dram.h"
#include "addrdec.h"
#include "stat-tool.h"
#include "l2cache.h"

#include "../cuda-sim/ptx-stats.h"
#include "../statwrapper.h"
#include "../abstract_hardware_model.h"
#include "../debug.h"
#include "../gpgpusim_entrypoint.h"
#include "../cuda-sim/cuda-sim.h"
#include "../trace.h"
#include "mem_latency_stat.h"
#include "power_stat.h"
#include "visualizer.h"
#include "stats.h"

#ifdef GPGPUSIM_POWER_MODEL
#include "power_interface.h"
#else
class  gpgpu_sim_wrapper {};
#endif

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)>(b))?(b):(a))

#define mem_phase 0
#define comp_phase 1
#define cache_phase 2

bool g_interactive_debugger_enabled=false;

unsigned long long  gpu_sim_cycle = 0;
unsigned long long  gpu_tot_sim_cycle = 0;
unsigned long long  GRAPE_phase_cycle = 0;
unsigned long long  GRAPE_tot_cycle = 0;
unsigned long long  GRAPE_sim_cycle = 0;


// performance counter for stalls due to congestion.
unsigned int gpu_stall_dramfull = 0; 
unsigned int gpu_stall_icnt2sh = 0;

/* Clock Domains */

#define  CORE  0x01
#define  L2    0x02
#define  DRAM  0x04
#define  ICNT  0x08
#define  GRAPE 0x0F 


#define MEM_LATENCY_STAT_IMPL




#include "mem_latency_stat.h"

void power_config::reg_options(class OptionParser * opp)
{


	  option_parser_register(opp, "-gpuwattch_xml_file", OPT_CSTR,
			  	  	  	  	 &g_power_config_name,"GPUWattch XML file",
	                   "gpuwattch.xml");

	   option_parser_register(opp, "-power_simulation_enabled", OPT_BOOL,
	                          &g_power_simulation_enabled, "Turn on power simulator (1=On, 0=Off)",
	                          "0");

	   option_parser_register(opp, "-power_per_cycle_dump", OPT_BOOL,
	                          &g_power_per_cycle_dump, "Dump detailed power output each cycle",
	                          "0");

	   // Output Data Formats
	   option_parser_register(opp, "-power_trace_enabled", OPT_BOOL,
	                          &g_power_trace_enabled, "produce a file for the power trace (1=On, 0=Off)",
	                          "0");

	   option_parser_register(opp, "-power_trace_zlevel", OPT_INT32,
	                          &g_power_trace_zlevel, "Compression level of the power trace output log (0=no comp, 9=highest)",
	                          "6");

	   option_parser_register(opp, "-steady_power_levels_enabled", OPT_BOOL,
	                          &g_steady_power_levels_enabled, "produce a file for the steady power levels (1=On, 0=Off)",
	                          "0");

	   option_parser_register(opp, "-steady_state_definition", OPT_CSTR,
			   	  &gpu_steady_state_definition, "allowed deviation:number of samples",
	                 	  "8:4");

}

void memory_config::reg_options(class OptionParser * opp)
{
    option_parser_register(opp, "-gpgpu_dram_scheduler", OPT_INT32, &scheduler_type, 
                                "0 = fifo, 1 = FR-FCFS (defaul)", "1");
    option_parser_register(opp, "-gpgpu_dram_partition_queues", OPT_CSTR, &gpgpu_L2_queue_config, 
                           "i2$:$2d:d2$:$2i",
                           "8:8:8:8");

    option_parser_register(opp, "-l2_ideal", OPT_BOOL, &l2_ideal, 
                           "Use a ideal L2 cache that always hit",
                           "0");
    option_parser_register(opp, "-gpgpu_cache:dl2", OPT_CSTR, &m_L2_config.m_config_string, 
                   "unified banked L2 data cache config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>}",
                   "64:128:8,L:B:m:N,A:16:4,4");
    option_parser_register(opp, "-gpgpu_cache:dl2_texture_only", OPT_BOOL, &m_L2_texure_only, 
                           "L2 cache used for texture only",
                           "1");
    option_parser_register(opp, "-gpgpu_n_mem", OPT_UINT32, &m_n_mem, 
                 "number of memory modules (e.g. memory controllers) in gpu",
                 "8");
    option_parser_register(opp, "-gpgpu_n_sub_partition_per_mchannel", OPT_UINT32, &m_n_sub_partition_per_memory_channel, 
                 "number of memory subpartition in each memory module",
                 "1");
    option_parser_register(opp, "-gpgpu_n_mem_per_ctrlr", OPT_UINT32, &gpu_n_mem_per_ctrlr, 
                 "number of memory chips per memory controller",
                 "1");
    option_parser_register(opp, "-gpgpu_memlatency_stat", OPT_INT32, &gpgpu_memlatency_stat, 
                "track and display latency statistics 0x2 enables MC, 0x4 enables queue logs",
                "0");
    option_parser_register(opp, "-gpgpu_frfcfs_dram_sched_queue_size", OPT_INT32, &gpgpu_frfcfs_dram_sched_queue_size, 
                "0 = unlimited (default); # entries per chip",
                "0");
    option_parser_register(opp, "-gpgpu_dram_return_queue_size", OPT_INT32, &gpgpu_dram_return_queue_size, 
                "0 = unlimited (default); # entries per chip",
                "0");
    option_parser_register(opp, "-gpgpu_dram_buswidth", OPT_UINT32, &busW, 
                 "default = 4 bytes (8 bytes per cycle at DDR)",
                 "4");
    option_parser_register(opp, "-gpgpu_dram_burst_length", OPT_UINT32, &BL, 
                 "Burst length of each DRAM request (default = 4 data bus cycle)",
                 "4");
    option_parser_register(opp, "-dram_data_command_freq_ratio", OPT_UINT32, &data_command_freq_ratio, 
                 "Frequency ratio between DRAM data bus and command bus (default = 2 times, i.e. DDR)",
                 "2");
    option_parser_register(opp, "-gpgpu_dram_timing_opt", OPT_CSTR, &gpgpu_dram_timing_opt, 
                "DRAM timing parameters = {nbk:tCCD:tRRD:tRCD:tRAS:tRP:tRC:CL:WL:tCDLR:tWR:nbkgrp:tCCDL:tRTPL}",
                "4:2:8:12:21:13:34:9:4:5:13:1:0:0");
    option_parser_register(opp, "-rop_latency", OPT_UINT32, &rop_latency,
                     "ROP queue latency (default 85)",
                     "85");
    option_parser_register(opp, "-dram_latency", OPT_UINT32, &dram_latency,
                     "DRAM latency (default 30)",
                     "30");

    m_address_mapping.addrdec_setoption(opp);
}

void shader_core_config::reg_options(class OptionParser * opp)
{
    option_parser_register(opp, "-gpgpu_simd_model", OPT_INT32, &model, 
                   "1 = post-dominator", "1");
    option_parser_register(opp, "-gpgpu_shader_core_pipeline", OPT_CSTR, &gpgpu_shader_core_pipeline_opt, 
                   "shader core pipeline config, i.e., {<nthread>:<warpsize>}",
                   "1024:32");
    option_parser_register(opp, "-gpgpu_tex_cache:l1", OPT_CSTR, &m_L1T_config.m_config_string, 
                   "per-shader L1 texture cache  (READ-ONLY) config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>:<rf>}",
                   "8:128:5,L:R:m:N,F:128:4,128:2");
    option_parser_register(opp, "-gpgpu_const_cache:l1", OPT_CSTR, &m_L1C_config.m_config_string, 
                   "per-shader L1 constant memory cache  (READ-ONLY) config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>} ",
                   "64:64:2,L:R:f:N,A:2:32,4" );
    option_parser_register(opp, "-gpgpu_cache:il1", OPT_CSTR, &m_L1I_config.m_config_string, 
                   "shader L1 instruction cache config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq>} ",
                   "4:256:4,L:R:f:N,A:2:32,4" );
    option_parser_register(opp, "-gpgpu_cache:dl1", OPT_CSTR, &m_L1D_config.m_config_string,
                   "per-shader L1 data cache config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq> | none}",
                   "none" );
    option_parser_register(opp, "-gpgpu_cache:dl1PrefL1", OPT_CSTR, &m_L1D_config.m_config_stringPrefL1,
                   "per-shader L1 data cache config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq> | none}",
                   "none" );
    option_parser_register(opp, "-gpgpu_cache:dl1PreShared", OPT_CSTR, &m_L1D_config.m_config_stringPrefShared,
                   "per-shader L1 data cache config "
                   " {<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>,<mshr>:<N>:<merge>,<mq> | none}",
                   "none" );
    option_parser_register(opp, "-gmem_skip_L1D", OPT_BOOL, &gmem_skip_L1D, 
                   "global memory access skip L1D cache (implements -Xptxas -dlcm=cg, default=no skip)",
                   "0");

    option_parser_register(opp, "-gpgpu_perfect_mem", OPT_BOOL, &gpgpu_perfect_mem, 
                 "enable perfect memory mode (no cache miss)",
                 "0");
    option_parser_register(opp, "-n_regfile_gating_group", OPT_UINT32, &n_regfile_gating_group,
                 "group of lanes that should be read/written together)",
                 "4");
    option_parser_register(opp, "-gpgpu_clock_gated_reg_file", OPT_BOOL, &gpgpu_clock_gated_reg_file,
                 "enable clock gated reg file for power calculations",
                 "0");
    option_parser_register(opp, "-gpgpu_clock_gated_lanes", OPT_BOOL, &gpgpu_clock_gated_lanes,
                 "enable clock gated lanes for power calculations",
                 "0");
    option_parser_register(opp, "-gpgpu_shader_registers", OPT_UINT32, &gpgpu_shader_registers, 
                 "Number of registers per shader core. Limits number of concurrent CTAs. (default 8192)",
                 "8192");
    option_parser_register(opp, "-gpgpu_shader_cta", OPT_UINT32, &max_cta_per_core, 
                 "Maximum number of concurrent CTAs in shader (default 8)",
                 "8");
    option_parser_register(opp, "-gpgpu_num_cta_barriers", OPT_UINT32, &max_barriers_per_cta,
                 "Maximum number of named barriers per CTA (default 16)",
                 "16");
    option_parser_register(opp, "-gpgpu_n_clusters", OPT_UINT32, &n_simt_clusters, 
                 "number of processing clusters",
                 "10");
    option_parser_register(opp, "-gpgpu_n_cores_per_cluster", OPT_UINT32, &n_simt_cores_per_cluster, 
                 "number of simd cores per cluster",
                 "3");
    option_parser_register(opp, "-gpgpu_n_cluster_ejection_buffer_size", OPT_UINT32, &n_simt_ejection_buffer_size, 
                 "number of packets in ejection buffer",
                 "8");
    option_parser_register(opp, "-gpgpu_n_ldst_response_buffer_size", OPT_UINT32, &ldst_unit_response_queue_size, 
                 "number of response packets in ld/st unit ejection buffer",
                 "2");
    option_parser_register(opp, "-gpgpu_shmem_size", OPT_UINT32, &gpgpu_shmem_size,
                 "Size of shared memory per shader core (default 16kB)",
                 "16384");
    option_parser_register(opp, "-gpgpu_shmem_size", OPT_UINT32, &gpgpu_shmem_sizeDefault,
                 "Size of shared memory per shader core (default 16kB)",
                 "16384");
    option_parser_register(opp, "-gpgpu_shmem_size_PrefL1", OPT_UINT32, &gpgpu_shmem_sizePrefL1,
                 "Size of shared memory per shader core (default 16kB)",
                 "16384");
    option_parser_register(opp, "-gpgpu_shmem_size_PrefShared", OPT_UINT32, &gpgpu_shmem_sizePrefShared,
                 "Size of shared memory per shader core (default 16kB)",
                 "16384");
    option_parser_register(opp, "-gpgpu_shmem_num_banks", OPT_UINT32, &num_shmem_bank, 
                 "Number of banks in the shared memory in each shader core (default 16)",
                 "16");
    option_parser_register(opp, "-gpgpu_shmem_limited_broadcast", OPT_BOOL, &shmem_limited_broadcast, 
                 "Limit shared memory to do one broadcast per cycle (default on)",
                 "1");
    option_parser_register(opp, "-gpgpu_shmem_warp_parts", OPT_INT32, &mem_warp_parts,  
                 "Number of portions a warp is divided into for shared memory bank conflict check ",
                 "2");
    option_parser_register(opp, "-gpgpu_warpdistro_shader", OPT_INT32, &gpgpu_warpdistro_shader, 
                "Specify which shader core to collect the warp size distribution from", 
                "-1");
    option_parser_register(opp, "-gpgpu_warp_issue_shader", OPT_INT32, &gpgpu_warp_issue_shader, 
                "Specify which shader core to collect the warp issue distribution from", 
                "0");
    option_parser_register(opp, "-gpgpu_local_mem_map", OPT_BOOL, &gpgpu_local_mem_map, 
                "Mapping from local memory space address to simulated GPU physical address space (default = enabled)", 
                "1");
    option_parser_register(opp, "-gpgpu_num_reg_banks", OPT_INT32, &gpgpu_num_reg_banks, 
                "Number of register banks (default = 8)", 
                "8");
    option_parser_register(opp, "-gpgpu_reg_bank_use_warp_id", OPT_BOOL, &gpgpu_reg_bank_use_warp_id,
             "Use warp ID in mapping registers to banks (default = off)",
             "0");
    option_parser_register(opp, "-gpgpu_operand_collector_num_units_sp", OPT_INT32, &gpgpu_operand_collector_num_units_sp,
                "number of collector units (default = 4)", 
                "4");
    option_parser_register(opp, "-gpgpu_operand_collector_num_units_sfu", OPT_INT32, &gpgpu_operand_collector_num_units_sfu,
                "number of collector units (default = 4)", 
                "4");
    option_parser_register(opp, "-gpgpu_operand_collector_num_units_mem", OPT_INT32, &gpgpu_operand_collector_num_units_mem,
                "number of collector units (default = 2)", 
                "2");
    option_parser_register(opp, "-gpgpu_operand_collector_num_units_gen", OPT_INT32, &gpgpu_operand_collector_num_units_gen,
                "number of collector units (default = 0)", 
                "0");
    option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_sp", OPT_INT32, &gpgpu_operand_collector_num_in_ports_sp,
                           "number of collector unit in ports (default = 1)", 
                           "1");
    option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_sfu", OPT_INT32, &gpgpu_operand_collector_num_in_ports_sfu,
                           "number of collector unit in ports (default = 1)", 
                           "1");
    option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_mem", OPT_INT32, &gpgpu_operand_collector_num_in_ports_mem,
                           "number of collector unit in ports (default = 1)", 
                           "1");
    option_parser_register(opp, "-gpgpu_operand_collector_num_in_ports_gen", OPT_INT32, &gpgpu_operand_collector_num_in_ports_gen,
                           "number of collector unit in ports (default = 0)", 
                           "0");
    option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_sp", OPT_INT32, &gpgpu_operand_collector_num_out_ports_sp,
                           "number of collector unit in ports (default = 1)", 
                           "1");
    option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_sfu", OPT_INT32, &gpgpu_operand_collector_num_out_ports_sfu,
                           "number of collector unit in ports (default = 1)", 
                           "1");
    option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_mem", OPT_INT32, &gpgpu_operand_collector_num_out_ports_mem,
                           "number of collector unit in ports (default = 1)", 
                           "1");
    option_parser_register(opp, "-gpgpu_operand_collector_num_out_ports_gen", OPT_INT32, &gpgpu_operand_collector_num_out_ports_gen,
                           "number of collector unit in ports (default = 0)", 
                           "0");
    option_parser_register(opp, "-gpgpu_coalesce_arch", OPT_INT32, &gpgpu_coalesce_arch, 
                            "Coalescing arch (default = 13, anything else is off for now)", 
                            "13");
    option_parser_register(opp, "-gpgpu_num_sched_per_core", OPT_INT32, &gpgpu_num_sched_per_core, 
                            "Number of warp schedulers per core", 
                            "1");
    option_parser_register(opp, "-gpgpu_max_insn_issue_per_warp", OPT_INT32, &gpgpu_max_insn_issue_per_warp,
                            "Max number of instructions that can be issued per warp in one cycle by scheduler",
                            "2");
    option_parser_register(opp, "-gpgpu_simt_core_sim_order", OPT_INT32, &simt_core_sim_order,
                            "Select the simulation order of cores in a cluster (0=Fix, 1=Round-Robin)",
                            "1");
    option_parser_register(opp, "-gpgpu_pipeline_widths", OPT_CSTR, &pipeline_widths_string,
                            "Pipeline widths "
                            "ID_OC_SP,ID_OC_SFU,ID_OC_MEM,OC_EX_SP,OC_EX_SFU,OC_EX_MEM,EX_WB",
                            "1,1,1,1,1,1,1" );
    option_parser_register(opp, "-gpgpu_num_sp_units", OPT_INT32, &gpgpu_num_sp_units,
                            "Number of SP units (default=1)",
                            "1");
    option_parser_register(opp, "-gpgpu_num_sfu_units", OPT_INT32, &gpgpu_num_sfu_units,
                            "Number of SF units (default=1)",
                            "1");
    option_parser_register(opp, "-gpgpu_num_mem_units", OPT_INT32, &gpgpu_num_mem_units,
                            "Number if ldst units (default=1) WARNING: not hooked up to anything",
                             "1");
    option_parser_register(opp, "-gpgpu_scheduler", OPT_CSTR, &gpgpu_scheduler_string,
                                "Scheduler configuration: < lrr | gto | two_level_active > "
                                "If two_level_active:<num_active_warps>:<inner_prioritization>:<outer_prioritization>"
                                "For complete list of prioritization values see shader.h enum scheduler_prioritization_type"
                                "Default: gto",
                                 "gto");
}



void gpgpu_sim_config::reg_options(option_parser_t opp)
{
    gpgpu_functional_sim_config::reg_options(opp);
    m_shader_config.reg_options(opp);
    m_memory_config.reg_options(opp);
    power_config::reg_options(opp);
   option_parser_register(opp, "-gpgpu_max_cycle", OPT_INT32, &gpu_max_cycle_opt, 
               "terminates gpu simulation early (0 = no limit)",
               "0");
   option_parser_register(opp, "-gpgpu_max_insn", OPT_INT32, &gpu_max_insn_opt, 
               "terminates gpu simulation early (0 = no limit)",
               "0");
   option_parser_register(opp, "-gpgpu_max_cta", OPT_INT32, &gpu_max_cta_opt, 
               "terminates gpu simulation early (0 = no limit)",
               "0");
   option_parser_register(opp, "-gpgpu_runtime_stat", OPT_CSTR, &gpgpu_runtime_stat, 
                  "display runtime statistics such as dram utilization {<freq>:<flag>}",
                  "10000:0");
   option_parser_register(opp, "-liveness_message_freq", OPT_INT64, &liveness_message_freq, 
               "Minimum number of seconds between simulation liveness messages (0 = always print)",
               "1");
   option_parser_register(opp, "-gpgpu_flush_l1_cache", OPT_BOOL, &gpgpu_flush_l1_cache,
                "Flush L1 cache at the end of each kernel call",
                "0");
   option_parser_register(opp, "-gpgpu_flush_l2_cache", OPT_BOOL, &gpgpu_flush_l2_cache,
                   "Flush L2 cache at the end of each kernel call",
                   "0");

   option_parser_register(opp, "-gpgpu_deadlock_detect", OPT_BOOL, &gpu_deadlock_detect, 
                "Stop the simulation at deadlock (1=on (default), 0=off)", 
                "1");
   option_parser_register(opp, "-gpgpu_ptx_instruction_classification", OPT_INT32, 
               &gpgpu_ptx_instruction_classification, 
               "if enabled will classify ptx instruction types per kernel (Max 255 kernels now)", 
               "0");
   option_parser_register(opp, "-gpgpu_ptx_sim_mode", OPT_INT32, &g_ptx_sim_mode, 
               "Select between Performance (default) or Functional simulation (1)", 
               "0");
   option_parser_register(opp, "-gpgpu_clock_domains", OPT_CSTR, &gpgpu_clock_domains, 
                  "Clock Domain Frequencies in MhZ, grape in KHz {<Core Clock>:<ICNT Clock>:<L2 Clock>:<DRAM Clock>:<GRAPE Clock>}",
                  "500.0:2000.0:2000.0:2000.0:0.1");
   option_parser_register(opp, "-gpgpu_max_concurrent_kernel", OPT_INT32, &max_concurrent_kernel,
                          "maximum kernels that can run concurrently on GPU", "8" );
   option_parser_register(opp, "-gpgpu_cflog_interval", OPT_INT32, &gpgpu_cflog_interval, 
               "Interval between each snapshot in control flow logger", 
               "0");
   option_parser_register(opp, "-visualizer_enabled", OPT_BOOL,
                          &g_visualizer_enabled, "Turn on visualizer output (1=On, 0=Off)",
                          "1");
   option_parser_register(opp, "-visualizer_outputfile", OPT_CSTR, 
                          &g_visualizer_filename, "Specifies the output log file for visualizer",
                          NULL);
   option_parser_register(opp, "-visualizer_zlevel", OPT_INT32,
                          &g_visualizer_zlevel, "Compression level of the visualizer output log (0=no comp, 9=highest)",
                          "6");
    option_parser_register(opp, "-trace_enabled", OPT_BOOL, 
                          &Trace::enabled, "Turn on traces",
                          "0");
    option_parser_register(opp, "-trace_components", OPT_CSTR, 
                          &Trace::config_str, "comma seperated list of traces to enable. "
                          "Complete list found in trace_streams.tup. "
                          "Default none",
                          "none");
    option_parser_register(opp, "-trace_sampling_core", OPT_INT32, 
                          &Trace::sampling_core, "The core which is printed using CORE_DPRINTF. Default 0",
                          "0");
    option_parser_register(opp, "-trace_sampling_memory_partition", OPT_INT32, 
                          &Trace::sampling_memory_partition, "The memory partition which is printed using MEMPART_DPRINTF. Default -1 (i.e. all)",
                          "-1");
//GRAPE addition

    option_parser_register(opp, "-control_goal", OPT_DOUBLE, &grape_goal, 
                             "latency goal",
                             "0");
    option_parser_register(opp, "-control_wcet", OPT_DOUBLE, &grape_wcet, 
                             "WCET",
                             "0");
    option_parser_register(opp, "-control_last_latency", OPT_DOUBLE, &last_latency, 
                             "Last Latency",
                             "0");
    option_parser_register(opp, "-control_power", OPT_DOUBLE, &power_limit, 
                             "Limit Power",
                             "0");
    option_parser_register(opp, "-goal_type", OPT_UINT32, &goal_type, 
                             "Goal Type",
                             "0");
    option_parser_register(opp, "-merlot_convex", OPT_UINT32, &merlot_convex, 
                             "Convexity",
                             "0");
    option_parser_register(opp, "-kernel_number", OPT_UINT32, &kernel_number, 
                             "number of kernel",
                             "1");
    option_parser_register(opp, "-kernel_goal", OPT_CSTR, &char_kernel_goal, 
                  "kernel goal",
                  "1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1");
    option_parser_register(opp, "-kernel_wcet", OPT_CSTR, &char_kernel_wcet, 
                  "kernel wcet",
                  "1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1");
    option_parser_register(opp, "-cta_wcet", OPT_CSTR, &char_cta_wcet, 
                  "cta wcet",
                  "1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1");
    option_parser_register(opp, "-cta_num", OPT_CSTR, &char_cta_num, 
                  "cta num",
                  "1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1");
    option_parser_register(opp, "-cta_kern", OPT_CSTR, &char_cta_kern, 
                  "cta kern",
                  "1:1:1:1:1:1:1:1:1:1:1:1:1:1:1:1");
/*
   option_parser_register(opp, "-last_lambda", OPT_UINT32, &last_lambda, 
                             "Last Lambda",
                             "0");
   option_parser_register(opp, "-last_lambda", OPT_DOUBLE, &est_speedup, 
                             "Lambda Speedup",
                             "0");*/
   ptx_file_line_stats_options(opp);
}

/////////////////////////////////////////////////////////////////////////////

void increment_x_then_y_then_z( dim3 &i, const dim3 &bound)
{
   i.x++;
   if ( i.x >= bound.x ) {
      i.x = 0;
      i.y++;
      if ( i.y >= bound.y ) {
         i.y = 0;
         if( i.z < bound.z ) 
            i.z++;
      }
   }
}

void gpgpu_sim::launch( kernel_info_t *kinfo )
{
   unsigned cta_size = kinfo->threads_per_cta();
   if ( cta_size > m_shader_config->n_thread_per_shader ) {
      printf("Execution error: Shader kernel CTA (block) size is too large for microarch config.\n");
      printf("                 CTA size (x*y*z) = %u, max supported = %u\n", cta_size, 
             m_shader_config->n_thread_per_shader );
      printf("                 => either change -gpgpu_shader argument in gpgpusim.config file or\n");
      printf("                 modify the CUDA source to decrease the kernel block size.\n");
      abort();
   }
   unsigned n=0;
   for(n=0; n < m_running_kernels.size(); n++ ) {
       if( (NULL==m_running_kernels[n]) || m_running_kernels[n]->done() ) {
           m_running_kernels[n] = kinfo;
           break;
       }
   }
   assert(n < m_running_kernels.size());
}

bool gpgpu_sim::can_start_kernel()
{
   for(unsigned n=0; n < m_running_kernels.size(); n++ ) {
       if( (NULL==m_running_kernels[n]) || m_running_kernels[n]->done() ) 
           return true;
   }
   return false;
}

bool gpgpu_sim::get_more_cta_left() const
{ 
   if (m_config.gpu_max_cta_opt != 0) {
      if( m_total_cta_launched >= m_config.gpu_max_cta_opt )
          return false;
   }
   for(unsigned n=0; n < m_running_kernels.size(); n++ ) {
       if( m_running_kernels[n] && !m_running_kernels[n]->no_more_ctas_to_run() ) 
           return true;
   }
   return false;
}

kernel_info_t *gpgpu_sim::select_kernel()
{
    for(unsigned n=0; n < m_running_kernels.size(); n++ ) {
        unsigned idx = (n+m_last_issued_kernel+1)%m_config.max_concurrent_kernel;
        if( m_running_kernels[idx] && !m_running_kernels[idx]->no_more_ctas_to_run() ) {
            m_last_issued_kernel=idx;
            // record this kernel for stat print if it is the first time this kernel is selected for execution  
            unsigned launch_uid = m_running_kernels[idx]->get_uid(); 
            if (std::find(m_executed_kernel_uids.begin(), m_executed_kernel_uids.end(), launch_uid) == m_executed_kernel_uids.end()) {
               m_executed_kernel_uids.push_back(launch_uid); 
               m_executed_kernel_names.push_back(m_running_kernels[idx]->name()); 
            }

            return m_running_kernels[idx];
        }
    }
    return NULL;
}

unsigned gpgpu_sim::finished_kernel()
{
    if( m_finished_kernel.empty() ) 
        return 0;
    unsigned result = m_finished_kernel.front();
    m_finished_kernel.pop_front();
    return result;
}

void gpgpu_sim::set_kernel_done( kernel_info_t *kernel ) 
{ 
    unsigned uid = kernel->get_uid();
    m_finished_kernel.push_back(uid);
    std::vector<kernel_info_t*>::iterator k;
    for( k=m_running_kernels.begin(); k!=m_running_kernels.end(); k++ ) {
        if( *k == kernel ) {
            *k = NULL;
            break;
        }
    }
    assert( k != m_running_kernels.end() ); 
}

void set_ptx_warp_size(const struct core_config * warp_size);

gpgpu_sim::gpgpu_sim( const gpgpu_sim_config &config ) 
    : gpgpu_t(config), m_config(config)
{ 
    m_shader_config = &m_config.m_shader_config;
    m_memory_config = &m_config.m_memory_config;
    set_ptx_warp_size(m_shader_config);
    ptx_file_line_stats_create_exposed_latency_tracker(m_config.num_shader());

#ifdef GPGPUSIM_POWER_MODEL
        m_gpgpusim_wrapper = new gpgpu_sim_wrapper(config.g_power_simulation_enabled,config.g_power_config_name);
#endif

    m_shader_stats = new shader_core_stats(m_shader_config);
    m_memory_stats = new memory_stats_t(m_config.num_shader(),m_shader_config,m_memory_config);
    average_pipeline_duty_cycle = (float *)malloc(sizeof(float));
    active_sms=(float *)malloc(sizeof(float));
    m_power_stats = new power_stat_t(m_shader_config,average_pipeline_duty_cycle,active_sms,m_shader_stats,m_memory_config,m_memory_stats);
  control_state[7].speedup = 1.0;
  control_state[6].speedup = 0.86;
  control_state[5].speedup = 0.72;
  control_state[4].speedup = 0.57;
  control_state[3].speedup = 0.42;
  control_state[2].speedup = 0.28;
  control_state[1].speedup = 0.142;
  control_state[7].powerup = 1.0;
  control_state[6].powerup = 0.81;
  control_state[5].powerup = 0.6889;
  control_state[4].powerup = 0.5776;
  control_state[3].powerup = 0.49;
  control_state[2].powerup = 0.3969;
  control_state[1].powerup = 0.3025;
  inputVariance=0;
  cumulative_latency =0;
  n_checkpoint=1;
  control_state[7].voltage = 1.0;
  control_state[6].voltage = 0.9;
  control_state[5].voltage = 0.83;
  control_state[4].voltage = 0.76;
  control_state[3].voltage = 0.70;
  control_state[2].voltage = 0.63;
  control_state[1].voltage = 0.55;
  /*if(!m_config.last_lambda){
  control_state[m_config.last_lambda].speedup=m_config.est_speedup;
  }*/
    gpu_sim_insn = 0;
    gpu_tot_sim_insn = 0;
    gpu_tot_issued_cta = 0;
    GRAPE_phase_time = 0.0;
    mGRAPE_time = 0.0;
    GRAPE_tot_insn =0;
    cumulative_time = 0.0;
    static_power = 0.0;
   movingAverage_coreFreq=0.0;
   movingAverage_memFreq=0.0;
max_w=1; 
for(int ii =0; ii <7; ii++){
non_convex[ii].speedup = 0;
non_convex[ii].powerup = 0;
non_convex[ii].valid=0;
}
lambda = 7;
last_lambda=0;
last_GRAPE_time = 0;
last_act = 7;
   temp_cta=0;
   coreScale_voltage = 1.0;
   memScale_voltage = 1.0;
   action = 0;
last_GRAPE_phase = comp_phase;
   Q = 0.00001;
   P = 1.0;
   P_minus = 0.0;
   H = 0.0;
   R = 0.001;
   K = 0.0;
   last_speedup = 1.0;
   x_hat_minus = 300000;
   x_hat = 300000;
   cycle_trigger =1;
   lambda_trigger = 39;
   power_value=120;
   Q_p = 0.00001;
   P_p = 1.0;
   P_minus_p = 0.0;
   H_p = 0.0;
   R_p = 0.001;
   K_p = 0.0;
   x_hat_minus_p = 120;
   x_hat_p = 120;
   u_p = 1.0;
   last_powerup = 1.0;
   child = 0;
   last_phase_MIPS=0;
   penalty_cycle=0;
   static_counter=1;
   iter_period=0;
   movingAverage_u = m_config.grape_wcet/m_config.grape_goal;
  control_state[7].power = 41.9;
  control_state[6].power = 30.888;
  control_state[5].power = 23.6217;
  control_state[4].power = 18.5149;
  control_state[3].power = 14.7421;
  control_state[2].power = 11.7576;
  control_state[1].power = 9.5778;
  
  control_state[7].conpower = 17.1;
  control_state[6].conpower = 12.6;
  control_state[5].conpower = 9.64;
  control_state[4].conpower = 7.55;
  control_state[3].conpower = 6.02;
  control_state[2].conpower = 4.79;
  control_state[1].conpower = 3.908;
  kernel_counter = 1;
  temp_cta = 0;
  

  sscanf(m_config.char_kernel_wcet,"%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", 
          &kernel_wcet[1],&kernel_wcet[2],&kernel_wcet[3],&kernel_wcet[4],&kernel_wcet[5],&kernel_wcet[6],&kernel_wcet[7],&kernel_wcet[8],&kernel_wcet[9],&kernel_wcet[10],&kernel_wcet[11],&kernel_wcet[12],&kernel_wcet[13],&kernel_wcet[14],&kernel_wcet[15],&kernel_wcet[16]);
  sscanf(m_config.char_kernel_goal,"%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", 
          &kernel_goal[1],&kernel_goal[2],&kernel_goal[3],&kernel_goal[4],&kernel_goal[5],&kernel_goal[6],&kernel_goal[7],&kernel_goal[8],&kernel_goal[9],&kernel_goal[10],&kernel_goal[11],&kernel_goal[12],&kernel_goal[13],&kernel_goal[14],&kernel_goal[15],&kernel_goal[16]);
  sscanf(m_config.char_cta_wcet,"%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf:%lf", 
          &c_time[1],&c_time[2],&c_time[3],&c_time[4],&c_time[5],&c_time[6],&c_time[7],&c_time[8],&c_time[9],&c_time[10],&c_time[11],&c_time[12],&c_time[13],&c_time[14],&c_time[15],&c_time[16]);
  printf("str %s\n",m_config.char_cta_wcet);
  for(int kk = 1;kk<17;kk++){
     printf("ctime %f\n",c_time[kk]);
  }
  printf("str %s\n",m_config.char_cta_num);
  sscanf(m_config.char_cta_num,"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d", 
          &c_CTA[1],&c_CTA[2],&c_CTA[3],&c_CTA[4],&c_CTA[5],&c_CTA[6],&c_CTA[7],&c_CTA[8],&c_CTA[9],&c_CTA[10],&c_CTA[11],&c_CTA[12],&c_CTA[13],&c_CTA[14],&c_CTA[15],&c_CTA[16]);
  for(int kk = 1;kk<17;kk++){
     printf("cCTA %d\n",c_CTA[kk]);
  }
  sscanf(m_config.char_cta_kern,"%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d", 
          &c_kernel[1],&c_kernel[2],&c_kernel[3],&c_kernel[4],&c_kernel[5],&c_kernel[6],&c_kernel[7],&c_kernel[8],&c_kernel[9],&c_kernel[10],&c_kernel[11],&c_kernel[12],&c_kernel[13],&c_kernel[14],&c_kernel[15],&c_kernel[16]);
  wcet_total=0;
  for(int kk = 1;kk<17;kk++){
     printf("ckernel %d\n",c_kernel[kk]);
  }
  for(int mm =1; mm<(m_config.kernel_number+1); mm++){
    printf("wcet %f wcetkern%f\n",wcet_total,kernel_wcet[mm]);
    wcet_total+=kernel_wcet[mm];
  }

/*   int kk =0;
   for(kk=0;kk<10;kk++){
   upper[kk]=0;
   lower[kk]=0;
   res_upper[kk]=0;
   res_lower[kk]=0;
   max_sampled[kk]=0;
   min_sampled[kk]=0;
   }*/
//GRAPE
    num_warp_mem_stall = 0;
    num_warp_comp_stall = 0;
    num_warp_waiting = 0;
    num_warp_tot = 0;
    num_warp_issued = 0;
    gpu_deadlock = false;


    m_cluster = new simt_core_cluster*[m_shader_config->n_simt_clusters];
    for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) 
        m_cluster[i] = new simt_core_cluster(this,i,m_shader_config,m_memory_config,m_shader_stats,m_memory_stats);

    m_memory_partition_unit = new memory_partition_unit*[m_memory_config->m_n_mem];
    m_memory_sub_partition = new memory_sub_partition*[m_memory_config->m_n_mem_sub_partition];
    for (unsigned i=0;i<m_memory_config->m_n_mem;i++) {
        m_memory_partition_unit[i] = new memory_partition_unit(i, m_memory_config, m_memory_stats);
        for (unsigned p = 0; p < m_memory_config->m_n_sub_partition_per_memory_channel; p++) {
            unsigned submpid = i * m_memory_config->m_n_sub_partition_per_memory_channel + p; 
            m_memory_sub_partition[submpid] = m_memory_partition_unit[i]->get_sub_partition(p); 
        }
    }

    icnt_wrapper_init();
    icnt_create(m_shader_config->n_simt_clusters,m_memory_config->m_n_mem_sub_partition);

    time_vector_create(NUM_MEM_REQ_STAT);
    fprintf(stdout, "GPGPU-Sim uArch: performance model initialization complete.\n");

    m_running_kernels.resize( config.max_concurrent_kernel, NULL );
    m_last_issued_kernel = 0;
    m_last_cluster_issue = 0;
    *average_pipeline_duty_cycle=0;
    *active_sms=0;

    last_liveness_message_time = 0;
}

int gpgpu_sim::shared_mem_size() const
{
   return m_shader_config->gpgpu_shmem_size;
}

int gpgpu_sim::num_registers_per_core() const
{
   return m_shader_config->gpgpu_shader_registers;
}

int gpgpu_sim::wrp_size() const
{
   return m_shader_config->warp_size;
}

int gpgpu_sim::shader_clock() const
{
   return m_config.core_freq/1000;
}

void gpgpu_sim::set_prop( cudaDeviceProp *prop )
{
   m_cuda_properties = prop;
}

const struct cudaDeviceProp *gpgpu_sim::get_prop() const
{
   return m_cuda_properties;
}

enum divergence_support_t gpgpu_sim::simd_model() const
{
   return m_shader_config->model;
}

void gpgpu_sim_config::init_clock_domains(void ) 
{
   sscanf(gpgpu_clock_domains,"%lf:%lf:%lf:%lf:%lf", 
          &core_freq, &icnt_freq, &l2_freq, &dram_freq, &grape_freq);
   core_freq = core_freq MhZ;
   icnt_freq = icnt_freq MhZ;
   l2_freq = l2_freq MhZ;
   dram_freq = dram_freq MhZ;
   grape_freq = grape_freq KhZ;        
   core_period = 1/core_freq;
   icnt_period = 1/icnt_freq;
   dram_period = 1/dram_freq;
   l2_period = 1/l2_freq;
   grape_period = 1/grape_freq;
   printf("GPGPU-Sim uArch: clock freqs: %lf:%lf:%lf:%lf:%lf\n",core_freq,icnt_freq,l2_freq,dram_freq,grape_freq);
   printf("GPGPU-Sim uArch: clock periods: %.20lf:%.20lf:%.20lf:%.20lf:%.20lf\n",core_period,icnt_period,l2_period,dram_period, grape_period);
}

void gpgpu_sim::reinit_clock_domains(void)
{
   core_time = 0;
   dram_time = 0;
   icnt_time = 0;
   l2_time = 0;
   grape_time = 0;
}

bool gpgpu_sim::active()
{
    if (m_config.gpu_max_cycle_opt && (gpu_tot_sim_cycle + gpu_sim_cycle) >= m_config.gpu_max_cycle_opt) 
       return false;
    if (m_config.gpu_max_insn_opt && (gpu_tot_sim_insn + gpu_sim_insn) >= m_config.gpu_max_insn_opt) 
       return false;
    if (m_config.gpu_max_cta_opt && (gpu_tot_issued_cta >= m_config.gpu_max_cta_opt) )
       return false;
    if (m_config.gpu_deadlock_detect && gpu_deadlock) 
       return false;
    for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) 
       if( m_cluster[i]->get_not_completed()>0 ) 
           return true;;
    for (unsigned i=0;i<m_memory_config->m_n_mem;i++) 
       if( m_memory_partition_unit[i]->busy()>0 )
           return true;;
    if( icnt_busy() )
        return true;
    if( get_more_cta_left() )
        return true;
    return false;
}
void gpgpu_sim::kernel_stats(){
     printf("kernel_counter %d",kernel_counter);
        kernel_counter ++;
     printf(" %d\n",kernel_counter);
}
void gpgpu_sim::init()
{
    // run a CUDA grid on the GPU microarchitecture simulator
    avg_pipeline=0;
    cum_avg_pipeline=0;
    for(int ii =0; ii<m_shader_config->n_simt_clusters;ii++){
      m_cluster[ii]->clst_cta_finished=0;
      m_cluster[ii]->core_reset();
    }
    control_counter=1;
    gpu_sim_cycle = 0;
    GRAPE_sim_cycle = 0;
    gpu_sim_insn = 0;
    last_GRAPE_sim_insn = 0;
    last_GRAPE_sim_cycle = 0;
    last_gpu_sim_insn = 0;
    last_gpu_sim_cycle = 0;
    m_total_cta_launched=0;
    reinit_clock_domains();
    set_param_gpgpu_num_shaders(m_config.num_shader());
    for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) 
       m_cluster[i]->reinit();
    m_shader_stats->new_grid();
    // initialize the control-flow, memory access, memory latency logger
    if (m_config.g_visualizer_enabled) {
        create_thread_CFlogger( m_config.num_shader(), m_shader_config->n_thread_per_shader, 0, m_config.gpgpu_cflog_interval );
    }
    shader_CTA_count_create( m_config.num_shader(), m_config.gpgpu_cflog_interval);
    if (m_config.gpgpu_cflog_interval != 0) {
       insn_warp_occ_create( m_config.num_shader(), m_shader_config->warp_size );
       shader_warp_occ_create( m_config.num_shader(), m_shader_config->warp_size, m_config.gpgpu_cflog_interval);
       shader_mem_acc_create( m_config.num_shader(), m_memory_config->m_n_mem, 4, m_config.gpgpu_cflog_interval);
       shader_mem_lat_create( m_config.num_shader(), m_config.gpgpu_cflog_interval);
       shader_cache_access_create( m_config.num_shader(), 3, m_config.gpgpu_cflog_interval);
       set_spill_interval (m_config.gpgpu_cflog_interval * 40);
    }

    if (g_network_mode)
       icnt_init();


     float ahead_time;
     printf("kwrnel counter %d max kernel %d\n",kernel_counter,m_config.kernel_number);
     if(kernel_counter>m_config.kernel_number){
        kernel_counter=1;
        n_checkpoint=1;
        //cumulative_latency =0;
        iter_period +=1;
     }
     printf("reset k %d c %d \n",kernel_counter, n_checkpoint);
     float remaining_time;
     if((kernel_counter==1)&&(iter_period==0)){
       min_resources = 1;
     }else{
       if(c_time[n_checkpoint]!=0){
         ahead_time = ((iter_period*kernel_wcet[m_config.kernel_number])+kernel_wcet[kernel_counter-1])-cumulative_latency;
         printf("ahd time %f cum %f\n",ahead_time, cumulative_latency);
         remaining_time = c_time[n_checkpoint]-kernel_wcet[kernel_counter-1];
         printf("ctime %f = %f - %f because %f\n",remaining_time,c_time[n_checkpoint],kernel_wcet[kernel_counter-1],c_time[n_checkpoint]);
       }else{
         ahead_time = ((iter_period*kernel_wcet[m_config.kernel_number])+kernel_wcet[kernel_counter-1])-cumulative_latency;
         printf("ahd time %f cum %f\n",ahead_time, cumulative_latency);
         remaining_time = kernel_wcet[kernel_counter]-kernel_wcet[kernel_counter-1];
         printf("not ctime %f = %f - %f\n",remaining_time,kernel_wcet[kernel_counter],kernel_wcet[kernel_counter-1]);
       }
       min_resources = remaining_time/(remaining_time+ahead_time);
     }

     
     printf("wcet %f %f \n",kernel_wcet[kernel_counter],cumulative_latency);
     printf("time %f %f %f\n",ahead_time,remaining_time,min_resources);
     printf("iteration %d\n",iter_period);
     //min_resources = kernel_wcet[kernel_counter]/kernel_goal[kernel_counter];
     lambda = ceil(7*min_resources);
     
    if((lambda <4)&&(c_CTA[1]!=0)){
      printf("lambda changed because c_CTA[1] = %d\n",c_CTA[1]);
      lambda = 4;
    }




    // McPAT initialization function. Called on first launch of GPU
#ifdef GPGPUSIM_POWER_MODEL
    if(m_config.g_power_simulation_enabled){
        init_mcpat(m_config, m_gpgpusim_wrapper, m_config.gpu_stat_sample_freq,  gpu_tot_sim_insn, gpu_sim_insn);
    }
#endif
//GRAPE init
   dynamic_dram_period = m_config.dram_period;
   dynamic_core_period = m_config.core_period;
   dynamic_l2_period = m_config.l2_period;
   dynamic_icnt_period = m_config.icnt_period;
printf("From kernel, lambda %d, min_resources %f\n",lambda,min_resources);
    var_freq = lambda;
    i_freq = lambda;
    action =0;
    grapeActuator();
}

void gpgpu_sim::update_stats() {
    m_memory_stats->memlatstat_lat_pw();
    gpu_tot_sim_cycle += gpu_sim_cycle;
    gpu_tot_sim_insn += gpu_sim_insn;
}

void gpgpu_sim::print_wcet(){
    int ll;
    
    printf("Ttime = ");
    for(ll =1; ll<7;ll++){
    printf("%f:",c_time[ll]);
    }
    printf("%f\n",c_time[8]);
    printf("Ccta = ");
    for(ll =1; ll<7;ll++){
    printf("%d:",c_CTA[ll]);
    }
    printf("%d\n",c_CTA[8]);
    printf("Kkernel = ");
    for(ll =1; ll<7;ll++){
    printf("%d:",c_kernel[ll]);
    }
    printf("%d\n",c_kernel[8]);

}

void gpgpu_sim::print_stats()
{
    
    ptx_file_line_stats_write_file();
    gpu_print_stat();
    kernel_counter ++;
    printf("kernel counter %d\n",kernel_counter);
    if (c_kernel[n_checkpoint]<kernel_counter){
         printf("init kern before %d\n",c_kernel[n_checkpoint]);  
         //c_time[n_checkpoint]=cumulative_latency + (core_time*1000);
         printf("arr %d %d, %d, %f\n",n_checkpoint,c_CTA[n_checkpoint],c_kernel[n_checkpoint],c_time[n_checkpoint]);
         n_checkpoint++;
    }
    if (g_network_mode) {
        printf("----------------------------Interconnect-DETAILS--------------------------------\n" );
        icnt_display_stats();
        icnt_display_overall_stats();
        printf("----------------------------END-of-Interconnect-DETAILS-------------------------\n" );
    }
    print_wcet();
}

void gpgpu_sim::deadlock_check()
{
   if (m_config.gpu_deadlock_detect && gpu_deadlock) {
      fflush(stdout);
      printf("\n\nGPGPU-Sim uArch: ERROR ** deadlock detected: last writeback core %u @ gpu_sim_cycle %u (+ gpu_tot_sim_cycle %u) (%u cycles ago)\n", 
             gpu_sim_insn_last_update_sid,
             (unsigned) gpu_sim_insn_last_update, (unsigned) (gpu_tot_sim_cycle-gpu_sim_cycle),
             (unsigned) (gpu_sim_cycle - gpu_sim_insn_last_update )); 
      unsigned num_cores=0;
      for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
         unsigned not_completed = m_cluster[i]->get_not_completed();
         if( not_completed ) {
             if ( !num_cores )  {
                 printf("GPGPU-Sim uArch: DEADLOCK  shader cores no longer committing instructions [core(# threads)]:\n" );
                 printf("GPGPU-Sim uArch: DEADLOCK  ");
                 m_cluster[i]->print_not_completed(stdout);
             } else if (num_cores < 8 ) {
                 m_cluster[i]->print_not_completed(stdout);
             } else if (num_cores >= 8 ) {
                 printf(" + others ... ");
             }
             num_cores+=m_shader_config->n_simt_cores_per_cluster;
         }
      }
      printf("\n");
      for (unsigned i=0;i<m_memory_config->m_n_mem;i++) {
         bool busy = m_memory_partition_unit[i]->busy();
         if( busy ) 
             printf("GPGPU-Sim uArch DEADLOCK:  memory partition %u busy\n", i );
      }
      if( icnt_busy() ) {
         printf("GPGPU-Sim uArch DEADLOCK:  iterconnect contains traffic\n");
         icnt_display_state( stdout );
      }
      printf("\nRe-run the simulator in gdb and use debug routines in .gdbinit to debug this\n");
      fflush(stdout);
      abort();
   }
}

/// printing the names and uids of a set of executed kernels (usually there is only one)
std::string gpgpu_sim::executed_kernel_info_string() 
{
   std::stringstream statout; 

   statout << "kernel_name = "; 
   for (unsigned int k = 0; k < m_executed_kernel_names.size(); k++) {
      statout << m_executed_kernel_names[k] << " "; 
   }
   statout << std::endl; 
   statout << "kernel_launch_uid = ";
   for (unsigned int k = 0; k < m_executed_kernel_uids.size(); k++) {
      statout << m_executed_kernel_uids[k] << " "; 
   }
   statout << std::endl; 

   return statout.str(); 
}
void gpgpu_sim::set_cache_config(std::string kernel_name,  FuncCache cacheConfig )
{
	m_special_cache_config[kernel_name]=cacheConfig ;
}

FuncCache gpgpu_sim::get_cache_config(std::string kernel_name)
{
	for (	std::map<std::string, FuncCache>::iterator iter = m_special_cache_config.begin(); iter != m_special_cache_config.end(); iter++){
		    std::string kernel= iter->first;
			if (kernel_name.compare(kernel) == 0){
				return iter->second;
			}
	}
	return (FuncCache)0;
}

bool gpgpu_sim::has_special_cache_config(std::string kernel_name)
{
	for (	std::map<std::string, FuncCache>::iterator iter = m_special_cache_config.begin(); iter != m_special_cache_config.end(); iter++){
	    	std::string kernel= iter->first;
			if (kernel_name.compare(kernel) == 0){
				return true;
			}
	}
	return false;
}


void gpgpu_sim::set_cache_config(std::string kernel_name)
{
	if(has_special_cache_config(kernel_name)){
		change_cache_config(get_cache_config(kernel_name));
	}else{
		change_cache_config(FuncCachePreferNone);
	}
}


void gpgpu_sim::change_cache_config(FuncCache cache_config)
{
	if(cache_config != m_shader_config->m_L1D_config.get_cache_status()){
		printf("FLUSH L1 Cache at configuration change between kernels\n");
		for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
			m_cluster[i]->cache_flush();
	    }
	}

	switch(cache_config){
	case FuncCachePreferNone:
		m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_string, FuncCachePreferNone);
		m_shader_config->gpgpu_shmem_size=m_shader_config->gpgpu_shmem_sizeDefault;
		break;
	case FuncCachePreferL1:
		if((m_shader_config->m_L1D_config.m_config_stringPrefL1 == NULL) || (m_shader_config->gpgpu_shmem_sizePrefL1 == (unsigned)-1))
		{
			printf("WARNING: missing Preferred L1 configuration\n");
			m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_string, FuncCachePreferNone);
			m_shader_config->gpgpu_shmem_size=m_shader_config->gpgpu_shmem_sizeDefault;

		}else{
			m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_stringPrefL1, FuncCachePreferL1);
			m_shader_config->gpgpu_shmem_size=m_shader_config->gpgpu_shmem_sizePrefL1;
		}
		break;
	case FuncCachePreferShared:
		if((m_shader_config->m_L1D_config.m_config_stringPrefShared == NULL) || (m_shader_config->gpgpu_shmem_sizePrefShared == (unsigned)-1))
		{
			printf("WARNING: missing Preferred L1 configuration\n");
			m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_string, FuncCachePreferNone);
			m_shader_config->gpgpu_shmem_size=m_shader_config->gpgpu_shmem_sizeDefault;
		}else{
			m_shader_config->m_L1D_config.init(m_shader_config->m_L1D_config.m_config_stringPrefShared, FuncCachePreferShared);
			m_shader_config->gpgpu_shmem_size=m_shader_config->gpgpu_shmem_sizePrefShared;
		}
		break;
	default:
		break;
	}
}


void gpgpu_sim::clear_executed_kernel_info()
{
   m_executed_kernel_names.clear();
   m_executed_kernel_uids.clear();
}
void gpgpu_sim::gpu_print_stat() 
{  
   double time_elapsed;
   FILE *statfout = stdout; 

   std::string kernel_info_str = executed_kernel_info_string(); 
   fprintf(statfout, "%s", kernel_info_str.c_str()); 
   //printf("sampled instruction %d = %d lower %d upper %d min %d max %d res lower %d res upper %d\n",kernel_counter,sampled_instruction[kernel_counter],lower[kernel_counter],upper[kernel_counter],min_sampled[kernel_counter],max_sampled[kernel_counter],res_lower[kernel_counter],res_upper[kernel_counter]);
   printf("gpu_sim_cycle = %lld\n", gpu_sim_cycle);
   printf("gpu_sim_insn = %lld\n", gpu_sim_insn);
   printf("gpu_ipc = %12.4f\n", (float)gpu_sim_insn / gpu_sim_cycle);
   printf("gpu_tot_sim_cycle = %lld\n", gpu_tot_sim_cycle+gpu_sim_cycle);
   printf("gpu_tot_sim_insn = %lld\n", gpu_tot_sim_insn+gpu_sim_insn);
   printf("GRAPE tot insn = %lld\n",GRAPE_tot_insn);
   printf("gpu_tot_ipc = %12.4f\n", (float)(gpu_tot_sim_insn+gpu_sim_insn) / (gpu_tot_sim_cycle+gpu_sim_cycle));
   printf("gpu_tot_issued_cta = %lld\n", gpu_tot_issued_cta);
   printf("grape_time = %12.12lf core_time = %12.12lf l2_time = %12.12lf icnt_time = %12.12lf dram_time = %12.12lf\n",grape_time, core_time, l2_time, icnt_time,dram_time);
   time_elapsed = MIN(grape_time, core_time);
   time_elapsed = MIN(time_elapsed, l2_time);
   time_elapsed = MIN(time_elapsed, icnt_time);
   time_elapsed = MIN(time_elapsed, dram_time);
   printf("lambda %d \ntime_elapsed = %.5f/%.5f\n",lambda, (time_elapsed*1000),kernel_goal[kernel_counter]);
   //cumulative_latency = (cumulative_time*1000)+m_config.last_latency;
   cumulative_latency += (time_elapsed*1000);
   printf("Kernel pipe util = %f\n",(cum_avg_pipeline/gpu_sim_cycle));
   printf("cumulative_latency =%.5f\n",cumulative_latency);
   printf("deadline app =%.5f/%.5f\n",cumulative_latency,wcet_total);
   printf("last checkpoint %d\n",n_checkpoint);
   printf("avg_MIPS =%.0f\n",MIPS);
   printf("u average = %f old WCET = %f \nest WCET =%.5f\nnew WCET =%.5f\n",movingAverage_u, kernel_wcet[kernel_counter],(time_elapsed*movingAverage_u*1000), MAX(kernel_wcet[kernel_counter],(time_elapsed*1000)*movingAverage_u));
   printf("avg core = %fMHz avg mem = %fMHz\n",(movingAverage_coreFreq*100),(movingAverage_memFreq*100));
   last_time_elapsed[kernel_counter]=(time_elapsed*1000);
   printf("gpu_stall_dramfull = %d\n", gpu_stall_dramfull);
   printf("gpu_stall_icnt2sh    = %d\n", gpu_stall_icnt2sh );

//GRAPE
   

   time_t curr_time;
   time(&curr_time);
   unsigned long long elapsed_time = MAX( curr_time - g_simulation_starttime, 1 );
   printf( "gpu_total_sim_rate=%u\n", (unsigned)( ( gpu_tot_sim_insn + gpu_sim_insn ) / elapsed_time ) );

   //shader_print_l1_miss_stat( stdout );
   shader_print_cache_stats(stdout);

   cache_stats core_cache_stats;
   core_cache_stats.clear();
   for(unsigned i=0; i<m_config.num_cluster(); i++){
       m_cluster[i]->get_cache_stats(core_cache_stats);
   }
   printf("\nTotal_core_cache_stats:\n");
   core_cache_stats.print_stats(stdout, "Total_core_cache_stats_breakdown");
   shader_print_scheduler_stat( stdout, false );

   m_shader_stats->print(stdout);
#ifdef GPGPUSIM_POWER_MODEL
   if(m_config.g_power_simulation_enabled){
	   m_gpgpusim_wrapper->print_power_kernel_stats(gpu_sim_cycle, gpu_tot_sim_cycle, gpu_tot_sim_insn + gpu_sim_insn, kernel_info_str, true );
	   mcpat_reset_perf_count(m_gpgpusim_wrapper);
   }
#endif

   // performance counter that are not local to one shader
   m_memory_stats->memlatstat_print(m_memory_config->m_n_mem,m_memory_config->nbk);
   for (unsigned i=0;i<m_memory_config->m_n_mem;i++)
      m_memory_partition_unit[i]->print(stdout);

   // L2 cache stats
   if(!m_memory_config->m_L2_config.disabled()){
       cache_stats l2_stats;
       struct cache_sub_stats l2_css;
       struct cache_sub_stats total_l2_css;
       l2_stats.clear();
       l2_css.clear();
       total_l2_css.clear();

       printf("\n========= L2 cache stats =========\n");
       for (unsigned i=0;i<m_memory_config->m_n_mem_sub_partition;i++){
           m_memory_sub_partition[i]->accumulate_L2cache_stats(l2_stats);
           m_memory_sub_partition[i]->get_L2cache_sub_stats(l2_css);

           fprintf( stdout, "L2_cache_bank[%d]: Access = %u, Miss = %u, Miss_rate = %.3lf, Pending_hits = %u, Reservation_fails = %u\n",
                    i, l2_css.accesses, l2_css.misses, (double)l2_css.misses / (double)l2_css.accesses, l2_css.pending_hits, l2_css.res_fails);

           total_l2_css += l2_css;
       }
       if (!m_memory_config->m_L2_config.disabled() && m_memory_config->m_L2_config.get_num_lines()) {
          //L2c_print_cache_stat();
          printf("L2_total_cache_accesses = %u\n", total_l2_css.accesses);
          printf("L2_total_cache_misses = %u\n", total_l2_css.misses);
          if(total_l2_css.accesses > 0)
              printf("L2_total_cache_miss_rate = %.4lf\n", (double)total_l2_css.misses/(double)total_l2_css.accesses);
          printf("L2_total_cache_pending_hits = %u\n", total_l2_css.pending_hits);
          printf("L2_total_cache_reservation_fails = %u\n", total_l2_css.res_fails);
          printf("L2_total_cache_breakdown:\n");
          l2_stats.print_stats(stdout, "L2_cache_stats_breakdown");
          total_l2_css.print_port_stats(stdout, "L2_cache");
       }
   }

   if (m_config.gpgpu_cflog_interval != 0) {
      spill_log_to_file (stdout, 1, gpu_sim_cycle);
      insn_warp_occ_print(stdout);
   }
   if ( gpgpu_ptx_instruction_classification ) {
      StatDisp( g_inst_classification_stat[g_ptx_kernel_count]);
      StatDisp( g_inst_op_classification_stat[g_ptx_kernel_count]);
   }

#ifdef GPGPUSIM_POWER_MODEL
   if(m_config.g_power_simulation_enabled){
       m_gpgpusim_wrapper->detect_print_steady_state(1,gpu_tot_sim_insn+gpu_sim_insn);
   }
#endif


   // Interconnect power stat print
   long total_simt_to_mem=0;
   long total_mem_to_simt=0;
   long temp_stm=0;
   long temp_mts = 0;
   for(unsigned i=0; i<m_config.num_cluster(); i++){
	   m_cluster[i]->get_icnt_stats(temp_stm, temp_mts);
	   total_simt_to_mem += temp_stm;
	   total_mem_to_simt += temp_mts;
   }
   printf("\nicnt_total_pkts_mem_to_simt=%ld\n", total_mem_to_simt);
   printf("icnt_total_pkts_simt_to_mem=%ld\n", total_simt_to_mem);

   time_vector_print();
   fflush(stdout);

   clear_executed_kernel_info(); 
}


// performance counter that are not local to one shader
unsigned gpgpu_sim::threads_per_core() const 
{ 
   return m_shader_config->n_thread_per_shader; 
}

void shader_core_ctx::mem_instruction_stats(const warp_inst_t &inst)
{
    unsigned active_count = inst.active_count(); 
    //this breaks some encapsulation: the is_[space] functions, if you change those, change this.
    switch (inst.space.get_type()) {
    case undefined_space:
    case reg_space:
        break;
    case shared_space:
        m_stats->gpgpu_n_shmem_insn += active_count; 
        break;
    case const_space:
        m_stats->gpgpu_n_const_insn += active_count;
        break;
    case param_space_kernel:
    case param_space_local:
        m_stats->gpgpu_n_param_insn += active_count;
        break;
    case tex_space:
        m_stats->gpgpu_n_tex_insn += active_count;
        break;
    case global_space:
    case local_space:
        if( inst.is_store() )
            m_stats->gpgpu_n_store_insn += active_count;
        else 
            m_stats->gpgpu_n_load_insn += active_count;
        break;
    default:
        abort();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Launches a cooperative thread array (CTA). 
 *  
 * @param kernel 
 *    object that tells us which kernel to ask for a CTA from 
 */

void shader_core_ctx::issue_block2core( kernel_info_t &kernel ) 
{
    set_max_cta(kernel);

    // find a free CTA context 
    unsigned free_cta_hw_id=(unsigned)-1;
    for (unsigned i=0;i<kernel_max_cta_per_shader;i++ ) {
      if( m_cta_status[i]==0 ) {
         free_cta_hw_id=i;
         break;
      }
    }
    assert( free_cta_hw_id!=(unsigned)-1 );

    // determine hardware threads and warps that will be used for this CTA
    int cta_size = kernel.threads_per_cta();

    // hw warp id = hw thread id mod warp size, so we need to find a range 
    // of hardware thread ids corresponding to an integral number of hardware
    // thread ids
    int padded_cta_size = cta_size; 
    if (cta_size%m_config->warp_size)
      padded_cta_size = ((cta_size/m_config->warp_size)+1)*(m_config->warp_size);
    unsigned start_thread = free_cta_hw_id * padded_cta_size;
    unsigned end_thread  = start_thread +  cta_size;

    // reset the microarchitecture state of the selected hardware thread and warp contexts
    reinit(start_thread, end_thread,false);
     
    // initalize scalar threads and determine which hardware warps they are allocated to
    // bind functional simulation state of threads to hardware resources (simulation) 
    warp_set_t warps;
    unsigned nthreads_in_block= 0;
    for (unsigned i = start_thread; i<end_thread; i++) {
        m_threadState[i].m_cta_id = free_cta_hw_id;
        unsigned warp_id = i/m_config->warp_size;
        nthreads_in_block += ptx_sim_init_thread(kernel,&m_thread[i],m_sid,i,cta_size-(i-start_thread),m_config->n_thread_per_shader,this,free_cta_hw_id,warp_id,m_cluster->get_gpu());
        m_threadState[i].m_active = true; 
        warps.set( warp_id );
    }
    assert( nthreads_in_block > 0 && nthreads_in_block <= m_config->n_thread_per_shader); // should be at least one, but less than max
    m_cta_status[free_cta_hw_id]=nthreads_in_block;

    // now that we know which warps are used in this CTA, we can allocate
    // resources for use in CTA-wide barrier operations
    m_barriers.allocate_barrier(free_cta_hw_id,warps);

    // initialize the SIMT stacks and fetch hardware
    init_warps( free_cta_hw_id, start_thread, end_thread);
    m_n_active_cta++;

    shader_CTA_count_log(m_sid, 1);
    printf("GPGPU-Sim uArch: core:%3d, cta:%2u initialized @(%lld,%lld)\n", m_sid, free_cta_hw_id, gpu_sim_cycle, gpu_tot_sim_cycle );
}

///////////////////////////////////////////////////////////////////////////////////////////

void dram_t::dram_log( int task ) 
{
   if (task == SAMPLELOG) {
      StatAddSample(mrqq_Dist, que_length());   
   } else if (task == DUMPLOG) {
      printf ("Queue Length DRAM[%d] ",id);StatDisp(mrqq_Dist);
   }
}

//Find next clock domain and increment its time
int gpgpu_sim::next_clock_domain(void) 
{
   double smallest = min3(core_time,icnt_time,dram_time);
   int mask = 0x00; 
   
   if ( l2_time <= smallest ) {
      smallest = l2_time;
      mask |= L2 ;
      //l2_time += m_config.l2_period;
      l2_time += dynamic_l2_period;
      //printf("l2_time = %.20lf\n", l2_time);
   }
   if ( icnt_time <= smallest ) {
      mask |= ICNT;
      //icnt_time += m_config.icnt_period;
      icnt_time += dynamic_icnt_period;
      //printf("icnt_time = %.20lf\n", icnt_time);
   }
   if ( dram_time <= smallest ) {
      mask |= DRAM;
      //dram_time += m_config.dram_period;
      dram_time += dynamic_dram_period;
      //printf("dram_time = %.20lf\n", dram_time);
   }
   if ( core_time <= smallest ) {
      mask |= CORE;
      //core_time += m_config.core_period;
      core_time += dynamic_core_period;
      //printf("core_time = %.20lf\n", core_time);
   }
   if(grape_time <= smallest){
      GRAPE_mask = 1;
      grape_time += m_config.grape_period;
      mGRAPE_time += m_config.grape_period;
      cumulative_time += m_config.grape_period;
      //printf("cumulative_time = %.20lf\n", cumulative_time);     
   }
   return mask;
}

void gpgpu_sim::issue_block2core()
{
    unsigned last_issued = m_last_cluster_issue; 
    for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
        unsigned idx = (i + last_issued + 1) % m_shader_config->n_simt_clusters;
        unsigned num = m_cluster[idx]->issue_block2core();
        if( num ) {
            m_last_cluster_issue=idx;
            m_total_cta_launched += num;
        }
    }
}

void gpgpu_sim::grapeMonitor(){

//GRAPE_time += 1/m_config.grape_freq;
GRAPE_phase_time = mGRAPE_time-last_GRAPE_time;
GRAPE_phase_insn = gpu_sim_insn-last_GRAPE_sim_insn;
GRAPE_tot_insn += GRAPE_phase_insn;
GRAPE_phase_cycle = gpu_sim_cycle-last_GRAPE_sim_cycle;
//printf("Total insn = %lld\n",gpu_tot_sim_cycle);
//printf("GRAPE inst.: %lld gpu_sim_insn : %lld last_gpu_sim_insn : %lld\n", GRAPE_phase_insn, gpu_sim_insn, last_GRAPE_sim_insn);
//printf("GRAPE cycle.: %lld, gpu_sim_cycle : %lld last_gpu_sim_cycle : %ld\n", GRAPE_phase_cycle,gpu_sim_cycle, last_GRAPE_sim_cycle);
//printf("phase time = %f GRAPE time = %f\n", GRAPE_phase_time,GRAPE_time);
MIPS = (float)GRAPE_tot_insn/(mGRAPE_time*1000000);
//phase_MIPS = (float)GRAPE_phase_insn/(GRAPE_phase_time*1000000);
phase_MIPS = (float)GRAPE_phase_insn;
printf("MIPS = %f Insn this phase = %f time %f last %f per %f\n", MIPS, phase_MIPS, mGRAPE_time, last_GRAPE_time,1/m_config.grape_freq);
last_GRAPE_sim_insn = gpu_sim_insn;
last_GRAPE_sim_cycle = gpu_sim_cycle;
last_GRAPE_time = mGRAPE_time;

}

void gpgpu_sim::grapeCluster(){
printf("total warp = %d mem stall = %d comp stall = %d waiting = %d issued = %d\n",num_warp_tot, num_warp_mem_stall, num_warp_comp_stall, num_warp_waiting, num_warp_issued);
   if((num_warp_mem_stall>(5*num_warp_comp_stall))&&(num_warp_mem_stall>num_warp_issued)){
    printf("Memory phase\n");
    GRAPE_phase = mem_phase;
    if(((float)num_warp_mem_stall/num_warp_tot)>0.25){
    printf("Cache phase\n");
    GRAPE_phase = cache_phase;
    } 
   }else{
    GRAPE_phase = comp_phase;
    printf("Compute phase\n");
   }
}

void gpgpu_sim::grapeMinMax(){
   if(m_config.grape_wcet!=0){
   min_resources = m_config.grape_wcet/m_config.grape_goal;
   printf("minimal resources = %f\n",min_resources);
   }
}

void gpgpu_sim::grapeAdaptive(){
  int parent=action-1;
  if(parent <0){
    parent =0;
  }

  if((action!=0)&&(non_convex[action].speedup==0)){
    non_convex[action].speedup= (phase_MIPS/last_phase_MIPS)*non_convex[parent].speedup;
    if(non_convex[action].speedup>1){
      non_convex[action].speedup=1;
    }
    printf("new speedup %f = %f * %f \n",non_convex[action].speedup,(phase_MIPS/last_phase_MIPS),non_convex[parent].speedup);
  }
  if(((abs(phase_MIPS-last_phase_MIPS)/phase_MIPS)<0.05)&&(control_counter>1)){  
    child=1;
  }else{
    child=0;
  }
  last_phase_MIPS=phase_MIPS;
}


void gpgpu_sim::lambdaEnergy(){
  double con_energy;
  double power_estimate = 1000;
  double best = 10000;
  control_counter=1;
  lambda = 0;
  P_minus_p = P_p+Q_p;
  x_hat_minus_p = x_hat_p;
  H_p=control_state[var_freq].powerup;
  K_p = (P_minus_p * H_p) / (H_p * P_minus_p *H_p + R_p);
  x_hat_p = x_hat_minus_p + K_p*(power_value - (H_p * x_hat_minus_p));
  P_p = (1-(K_p*H_p))*P_minus_p;
  min_resources = kernel_wcet[kernel_counter]/kernel_goal[kernel_counter];
  if((inputVariance<0)||(inputVariance>1)){
     inputVariance=0;
  }
  for(int i = 2; i<=7; i++){  
    if(control_state[i].speedup > min_resources){
        power_estimate = (control_state[i].powerup*x_hat_p)+control_state[i].power;
	con_energy = (power_estimate*(min_resources/control_state[i].speedup))+(47*(1-(min_resources/control_state[i].speedup)));//??
	printf("%d spdup = %f power = %f energy = %f\n",i,control_state[i].speedup,power_estimate, con_energy);
	  if(con_energy < best){
             best = con_energy;
             lambda = i-inputVariance;
          }
    }
  }

   
   if(lambda!=last_lambda){
   for(int ii =0; ii <7;ii++){  
    non_convex[ii].valid=1;
    non_convex[ii].speedup=0;
    printf("id %d sp %f pw %f \n",ii,non_convex[ii].speedup,non_convex[ii].powerup);
   }
   action =0;
   }
   non_convex[0].speedup=control_state[lambda].speedup;
   last_lambda = lambda;
   printf("lambda %d \n", lambda);
   movingAverage_u=0;
}

void gpgpu_sim::grapeOptimizer(){

  if((non_convex[action].speedup<(m_config.grape_wcet/m_config.grape_goal))&&(non_convex[action].speedup>0)){
   non_convex[action].valid=0;
   action = action-1;
   if(action<0){
     action=0;
   }
  }

  if((non_convex[action+1].valid)){
     if(child){
     action += 1;
     }
     if(action>2){
     action=2;
     }
  }
  if(last_GRAPE_phase != GRAPE_phase){
    action = 0;
  }

  if ((GRAPE_phase == mem_phase)||(GRAPE_phase==cache_phase)){
           var_freq=lambda-1;
        if(num_warp_mem_stall<num_warp_comp_stall){
           var_freq=lambda;
        }
        i_freq=7-action;
        if(i_freq<lambda){
           i_freq=lambda;
           non_convex[action+1].valid=0;
        }
   }else{
    var_freq=lambda;
    i_freq=lambda-action;
    if(i_freq<5){
      i_freq=5;
      non_convex[action+1].valid=0;
    }
    if(num_warp_mem_stall>num_warp_comp_stall){
      printf("Before mem %d",i_freq);
      if(i_freq<6){
        i_freq=6;
      }
      if(i_freq<lambda){
        i_freq=lambda;
      }
      printf(" After mem %d\n",i_freq);
    }
   }
  if(var_freq<4){
    var_freq=4;
  }
  if(lambda ==7){
   var_freq=7;
    i_freq=7;
  }
  movingAverage_coreFreq = ((movingAverage_coreFreq*(control_counter-1))+var_freq)/control_counter;
  movingAverage_memFreq = ((movingAverage_memFreq*(control_counter-1))+i_freq)/control_counter;
  static_power = ((static_power*(static_counter-1))+control_state[var_freq].power+control_state[i_freq].conpower)/static_counter;
  control_counter++;
  static_counter++;
  cycle_trigger = var_freq*1250;
  float temp_spd;
  if(non_convex[action].speedup==0){
  temp_spd=control_state[lambda].speedup;
  }else{
  temp_spd=non_convex[action].speedup;
  }
  movingAverage_u = ((movingAverage_u*(control_counter-1))+temp_spd)/control_counter;
  
  if(last_act!=action){
    penalty_cycle=512;
  }
 
  last_act = action;
  last_GRAPE_phase = GRAPE_phase;
    num_warp_mem_stall = 0;
    num_warp_comp_stall = 0;
    num_warp_waiting = 0;
    num_warp_tot = 0;
    num_warp_issued = 0;
  printf("dynamic power = %f\n",power_value);
  printf("static_power =%f\n", static_power);
  printf("total power = %f\n",(power_value+static_power));
  printf("action %d core freq = %d00MHz memory freq = %d00MHz\n",action,var_freq, i_freq);
  printf("sp %f\n",non_convex[action].speedup);
  
  printf("avg core = %fMHz avg mem = %fMHz\n",(movingAverage_coreFreq*100),(movingAverage_memFreq*100));
}

void gpgpu_sim::grapeActuator(){
  dynamic_dram_period = 1/((float)i_freq*132000000);
  dynamic_l2_period = 1/((float)i_freq*100000000);
  dynamic_icnt_period = 1/((float)i_freq*100000000);
  dynamic_core_period = 1/((float)var_freq*100000000);
  //printf("dynamic_core_period = %.5lf dynamic_dram_period %.5lf dynamic_l2_period %.5lf\n",(dynamic_core_period*1000000), (dynamic_dram_period*1000000),(dynamic_l2_period*1000000));
  memScale_voltage = control_state[i_freq].voltage*control_state[i_freq].voltage;
  coreScale_voltage = control_state[var_freq].voltage*control_state[var_freq].voltage;
  printf("memScale_voltage %d = %f coreScale_voltage %d = %f\n",i_freq,memScale_voltage,var_freq,coreScale_voltage);
}

unsigned long long g_single_step=0; // set this in gdb to single step the pipeline

void gpgpu_sim::cycle()
{
   GRAPE_mask = 0;
   int clock_mask = next_clock_domain();
   
   if(GRAPE_mask==1){

   }
   if (clock_mask & CORE ) {
      //printf("CORE\n");
       // shader core loading (pop from ICNT into core) follows CORE clock
      for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) 
         m_cluster[i]->icnt_cycle(); 
   }
    if (clock_mask & ICNT) {
       //printf("ICNT\n");
        // pop from memory controller to interconnect
        for (unsigned i=0;i<m_memory_config->m_n_mem_sub_partition;i++) {
            mem_fetch* mf = m_memory_sub_partition[i]->top();
            if (mf) {
                unsigned response_size = mf->get_is_write()?mf->get_ctrl_size():mf->size();
                if ( ::icnt_has_buffer( m_shader_config->mem2device(i), response_size ) ) {
                    if (!mf->get_is_write()) 
                       mf->set_return_timestamp(gpu_sim_cycle+gpu_tot_sim_cycle);
                    mf->set_status(IN_ICNT_TO_SHADER,gpu_sim_cycle+gpu_tot_sim_cycle);
                    ::icnt_push( m_shader_config->mem2device(i), mf->get_tpc(), mf, response_size );
                    m_memory_sub_partition[i]->pop();
                } else {
                    gpu_stall_icnt2sh++;
                }
            } else {
               m_memory_sub_partition[i]->pop();
            }
        }
    }

   if (clock_mask & DRAM) {
      //printf("DRAM\n");
      for (unsigned i=0;i<m_memory_config->m_n_mem;i++){
         m_memory_partition_unit[i]->dram_cycle(); // Issue the dram command (scheduler + delay model)
         // Update performance counters for DRAM
         m_memory_partition_unit[i]->set_dram_power_stats(m_power_stats->pwr_mem_stat->n_cmd[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_activity[CURRENT_STAT_IDX][i],
                        m_power_stats->pwr_mem_stat->n_nop[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_act[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_pre[CURRENT_STAT_IDX][i],
                        m_power_stats->pwr_mem_stat->n_rd[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_wr[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_req[CURRENT_STAT_IDX][i]);

      }
   }

   // L2 operations follow L2 clock domain
   if (clock_mask & L2) {
       //printf("L2\n");
       m_power_stats->pwr_mem_stat->l2_cache_stats[CURRENT_STAT_IDX].clear();
      for (unsigned i=0;i<m_memory_config->m_n_mem_sub_partition;i++) {
          //move memory request from interconnect into memory partition (if not backed up)
          //Note:This needs to be called in DRAM clock domain if there is no L2 cache in the system
          if ( m_memory_sub_partition[i]->full() ) {
             gpu_stall_dramfull++;
          } else {
              mem_fetch* mf = (mem_fetch*) icnt_pop( m_shader_config->mem2device(i) );
              m_memory_sub_partition[i]->push( mf, gpu_sim_cycle + gpu_tot_sim_cycle );
          }
          m_memory_sub_partition[i]->cache_cycle(gpu_sim_cycle+gpu_tot_sim_cycle);
          m_memory_sub_partition[i]->accumulate_L2cache_stats(m_power_stats->pwr_mem_stat->l2_cache_stats[CURRENT_STAT_IDX]);
       }
   }

   if (clock_mask & ICNT) {
     // printf("ICNT\n");
      icnt_transfer();
   }
   int active_core=0;
   if (clock_mask & CORE) {
      // L1 cache + shader core pipeline stages
      m_power_stats->pwr_mem_stat->core_cache_stats[CURRENT_STAT_IDX].clear();
      if((GRAPE_phase == cache_phase)&&(lambda!=7)){
        gpu_CTA_limit = 1;
      }else{
        gpu_CTA_limit = 8;
      }
      penalty_cycle--;
      for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
         if ((m_cluster[i]->get_not_completed() || get_more_cta_left())&&(penalty_cycle<1)) {
               m_cluster[i]->clst_warp_waiting = 0;
               m_cluster[i]->clst_warp_mem_stall= 0;
               m_cluster[i]->clst_warp_comp_stall= 0;
               m_cluster[i]->clst_warp_issued= 0;
               m_cluster[i]->clst_warp_tot= 0;
               m_cluster[i]->core_cycle();
               m_cluster[i]->clst_cta_limit = gpu_CTA_limit;
               num_warp_waiting += m_cluster[i]->clst_warp_waiting;
               num_warp_mem_stall += m_cluster[i]->clst_warp_mem_stall;
               num_warp_comp_stall += m_cluster[i]->clst_warp_comp_stall;
               num_warp_issued += m_cluster[i]->clst_warp_issued;
               num_warp_tot += m_cluster[i]->clst_warp_tot;
               *active_sms+=m_cluster[i]->get_n_active_sms();
               active_core++;
         }
         // Update core icnt/cache stats for GPUWattch
         m_cluster[i]->get_icnt_stats(m_power_stats->pwr_mem_stat->n_simt_to_mem[CURRENT_STAT_IDX][i], m_power_stats->pwr_mem_stat->n_mem_to_simt[CURRENT_STAT_IDX][i]);
         m_cluster[i]->get_cache_stats(m_power_stats->pwr_mem_stat->core_cache_stats[CURRENT_STAT_IDX]);
      }
      float temp=0;
      for (unsigned i=0;i<m_shader_config->num_shader();i++){
        temp+=m_shader_stats->m_pipeline_duty_cycle[i];
      }
      
      temp=temp/m_shader_config->num_shader();
      *average_pipeline_duty_cycle=((*average_pipeline_duty_cycle)+temp);
        //cout<<"Average pipeline duty cycle: "<<*average_pipeline_duty_cycle<<endl;
      if(active_core!=0){
      avg_pipeline = avg_pipeline + (temp*(15/active_core));
      cum_avg_pipeline = cum_avg_pipeline + (temp*(15/active_core));
      }
      if( g_single_step && ((gpu_sim_cycle+gpu_tot_sim_cycle) >= g_single_step) ) {
          asm("int $03");
      }
      gpu_sim_cycle++;
      GRAPE_sim_cycle++;

      if( g_interactive_debugger_enabled ) 
         gpgpu_debug();

      // McPAT main cycle (interface with McPAT)
#ifdef GPGPUSIM_POWER_MODEL
      if(m_config.g_power_simulation_enabled){
         temp_powervalue = mcpat_cycle(m_config, getShaderCoreConfig(), m_gpgpusim_wrapper, m_power_stats, m_config.gpu_stat_sample_freq, gpu_tot_sim_cycle, gpu_sim_cycle, gpu_tot_sim_insn, gpu_sim_insn, coreScale_voltage, memScale_voltage,var_freq,(i_freq*1.32));
	if(temp_powervalue>1){	
	power_value = temp_powervalue;
	}
      }
#endif

   //int interrupt_cycle = ceil((kernel_wcet[kernel_counter]*700000)/64);
   float interrupt_cycle = wcet_total/16;
   if(c_time[n_checkpoint]!=0){
   if((m_cluster[0]->clst_cta_finished==c_CTA[n_checkpoint])&&(c_kernel[n_checkpoint]==kernel_counter)){
         int CTA_checkpoint = 0;
         for(int ii =0; ii<m_shader_config->n_simt_clusters;ii++){
             CTA_checkpoint += m_cluster[ii]->clst_cta_finished;
         }
         printf("Total CTA %d\n",CTA_checkpoint);
         printf("Ctime at %d before %f ",n_checkpoint,c_time[n_checkpoint]);
         //c_time[n_checkpoint]=MAX(c_time[n_checkpoint],(cumulative_latency + (core_time*1000)));
         printf("time %f after %f\n",(cumulative_latency + (core_time*1000)),c_time[n_checkpoint]);
         float remaining_time;
         if(n_checkpoint!=16){
           if(c_kernel[n_checkpoint]==c_kernel[n_checkpoint+1]){
             remaining_time = c_time[n_checkpoint+1]-c_time[n_checkpoint];
             printf("1 %f = %f-%f\n",remaining_time,c_time[n_checkpoint+1],c_time[n_checkpoint]);
           }else{
             remaining_time = kernel_wcet[kernel_counter]-c_time[n_checkpoint];
             printf("2 %f = %f-%f\n",remaining_time,kernel_wcet[kernel_counter],c_time[n_checkpoint]);
           }
         }else{
           remaining_time = kernel_wcet[kernel_counter]-c_time[n_checkpoint];
           printf("3 %f = %f-%f\n",remaining_time,kernel_wcet[kernel_counter],c_time[n_checkpoint]);
         }
         float ahead_time = ((iter_period*kernel_wcet[m_config.kernel_number])+c_time[n_checkpoint])-(cumulative_latency + (core_time*1000));
         printf("counter %d kerntime %f cum %f ahead %f remaining %f\n",kernel_counter,kernel_wcet[kernel_counter],(cumulative_latency + (core_time*1000)),ahead_time,remaining_time);
         printf("min res before %f ",min_resources);
         min_resources = remaining_time/(remaining_time+ahead_time);
         printf("after %f\n",min_resources);
         //grapeOptimizer();

         lambda = ceil(7*min_resources);
         
         printf("lambda %d\n",lambda);
         float pipeline_utilities =0;
         pipeline_utilities =avg_pipeline/(gpu_sim_cycle-last_gpu_sim_cycle);
         printf("Pipeline util = %f %f\n",pipeline_utilities,temp);
         
         if(temp<0.2){
           if(lambda <4){
           i_freq = 4;
           var_freq= 4;
           }else{
           i_freq = lambda+1;
           var_freq= lambda;
           }
         }else if(temp>0.8){
           if(lambda <4){
           i_freq = 3;
           var_freq= 4;
           }else{
           i_freq = lambda;
           var_freq= lambda;
           }
         }else{
           if(lambda <4){
           i_freq = 4;
           var_freq= 4;
           }else{
           i_freq = lambda;
           var_freq= lambda;
           }
         }
         if(i_freq>7){
           i_freq=7;
         }
         printf("freq %d %d\n",var_freq,i_freq);
         grapeActuator();
         
         
         
         last_gpu_sim_cycle=gpu_sim_cycle;
	 gpu_sim_cycle += 512;
         avg_pipeline=0;
         n_checkpoint++;

   }
   }

  
      issue_block2core();
      
      // Depending on configuration, flush the caches once all of threads are completed.
      int all_threads_complete = 1;
      if (m_config.gpgpu_flush_l1_cache) {
         for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
            if (m_cluster[i]->get_not_completed() == 0)
                m_cluster[i]->cache_flush();
            else
               all_threads_complete = 0 ;
         }
      }

      if(m_config.gpgpu_flush_l2_cache){
          if(!m_config.gpgpu_flush_l1_cache){
              for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
                  if (m_cluster[i]->get_not_completed() != 0){
                      all_threads_complete = 0 ;
                      break;
                  }
              }
          }

         if (all_threads_complete && !m_memory_config->m_L2_config.disabled() ) {
            printf("Flushed L2 caches...\n");
            if (m_memory_config->m_L2_config.get_num_lines()) {
               int dlc = 0;
               for (unsigned i=0;i<m_memory_config->m_n_mem;i++) {
                  dlc = m_memory_sub_partition[i]->flushL2();
                  assert (dlc == 0); // need to model actual writes to DRAM here
                  printf("Dirty lines flushed from L2 %d is %d\n", i, dlc  );
               }
            }
         }
      }

      if (!(gpu_sim_cycle % m_config.gpu_stat_sample_freq)) {
         time_t days, hrs, minutes, sec;
         time_t curr_time;
         time(&curr_time);
         unsigned long long  elapsed_time = MAX(curr_time - g_simulation_starttime, 1);
         if ( (elapsed_time - last_liveness_message_time) >= m_config.liveness_message_freq ) {
            days    = elapsed_time/(3600*24);
            hrs     = elapsed_time/3600 - 24*days;
            minutes = elapsed_time/60 - 60*(hrs + 24*days);
            sec = elapsed_time - 60*(minutes + 60*(hrs + 24*days));
            /*printf("GPGPU-Sim uArch: cycles simulated: %lld  inst.: %lld (ipc=%4.1f) sim_rate=%u (inst/sec) elapsed = %u:%u:%02u:%02u / %s", 
                   gpu_tot_sim_cycle + gpu_sim_cycle, gpu_tot_sim_insn + gpu_sim_insn, 
                   (double)gpu_sim_insn/(double)gpu_sim_cycle,
                   (unsigned)((gpu_tot_sim_insn+gpu_sim_insn) / elapsed_time),
                   (unsigned)days,(unsigned)hrs,(unsigned)minutes,(unsigned)sec,
                   ctime(&curr_time));*/
            fflush(stdout);
            last_liveness_message_time = elapsed_time; 
         }
         visualizer_printstat();
         m_memory_stats->memlatstat_lat_pw();
         if (m_config.gpgpu_runtime_stat && (m_config.gpu_runtime_stat_flag != 0) ) {
            if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_BW_STAT) {
               for (unsigned i=0;i<m_memory_config->m_n_mem;i++) 
                  m_memory_partition_unit[i]->print_stat(stdout);
               printf("maxmrqlatency = %d \n", m_memory_stats->max_mrq_latency);
               printf("maxmflatency = %d \n", m_memory_stats->max_mf_latency);
            }
            if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_SHD_INFO) 
               shader_print_runtime_stat( stdout );
            if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_L1MISS) 
               shader_print_l1_miss_stat( stdout );
            if (m_config.gpu_runtime_stat_flag & GPU_RSTAT_SCHED) 
               shader_print_scheduler_stat( stdout, false );
         }
      }

      if (!(gpu_sim_cycle % 500)) {
         static_power = ((static_power*(static_counter-1))+control_state[var_freq].power+control_state[i_freq].conpower)/static_counter;
         static_counter++;
         printf("static_power =%f\n", static_power);
      }
      if (!(gpu_sim_cycle % 20000)) {
         // deadlock detection 
         if (m_config.gpu_deadlock_detect && gpu_sim_insn == last_gpu_sim_insn) {
            gpu_deadlock = true;
         } else {
            last_gpu_sim_insn = gpu_sim_insn;
         }
      }
      try_snap_shot(gpu_sim_cycle);
      spill_log_to_file (stdout, 0, gpu_sim_cycle);
   }

}


void shader_core_ctx::dump_warp_state( FILE *fout ) const
{
   fprintf(fout, "\n");
   fprintf(fout, "per warp functional simulation status:\n");
   for (unsigned w=0; w < m_config->max_warps_per_shader; w++ ) 
       m_warp[w].print(fout);
}

void gpgpu_sim::dump_pipeline( int mask, int s, int m ) const
{
/*
   You may want to use this function while running GPGPU-Sim in gdb.
   One way to do that is add the following to your .gdbinit file:
 
      define dp
         call g_the_gpu.dump_pipeline_impl((0x40|0x4|0x1),$arg0,0)
      end
 
   Then, typing "dp 3" will show the contents of the pipeline for shader core 3.
*/

   printf("Dumping pipeline state...\n");
   if(!mask) mask = 0xFFFFFFFF;
   for (unsigned i=0;i<m_shader_config->n_simt_clusters;i++) {
      if(s != -1) {
         i = s;
      }
      if(mask&1) m_cluster[m_shader_config->sid_to_cluster(i)]->display_pipeline(i,stdout,1,mask & 0x2E);
      if(s != -1) {
         break;
      }
   }
   if(mask&0x10000) {
      for (unsigned i=0;i<m_memory_config->m_n_mem;i++) {
         if(m != -1) {
            i=m;
         }
         printf("DRAM / memory controller %u:\n", i);
         if(mask&0x100000) m_memory_partition_unit[i]->print_stat(stdout);
         if(mask&0x1000000)   m_memory_partition_unit[i]->visualize();
         if(mask&0x10000000)   m_memory_partition_unit[i]->print(stdout);
         if(m != -1) {
            break;
         }
      }
   }
   fflush(stdout);
}

const struct shader_core_config * gpgpu_sim::getShaderCoreConfig()
{
   return m_shader_config;
}

const struct memory_config * gpgpu_sim::getMemoryConfig()
{
   return m_memory_config;
}

simt_core_cluster * gpgpu_sim::getSIMTCluster()
{
   return *m_cluster;
}

