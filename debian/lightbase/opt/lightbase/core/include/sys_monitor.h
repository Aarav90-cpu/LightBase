#ifndef LIGHTBASE_SYS_MONITOR_H
#define LIGHTBASE_SYS_MONITOR_H

// Structural frame layout capturing host machine states
typedef struct {
    double cpu_usage_percentage;
    unsigned long total_mem_kb;
    unsigned long free_mem_kb;
    unsigned long avail_mem_kb;
} SystemMetrics;

// System function declarations to hook the Linux kernel tracking maps
SystemMetrics gather_system_performance();

#endif // LIGHTBASE_SYS_MONITOR_H