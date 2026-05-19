#include "sys_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

SystemMetrics gather_system_performance() {
    SystemMetrics metrics = {0.0, 0, 0, 0};
    char buffer[256];

    // 1. PARSE KERNEL MEMORY MAPS VIA /proc/meminfo
    FILE* mem_file = fopen("/proc/meminfo", "r");
    if (mem_file) {
        while (fgets(buffer, sizeof(buffer), mem_file)) {
            if (strncmp(buffer, "MemTotal:", 9) == 0) {
                sscanf(buffer, "MemTotal: %lu", &metrics.total_mem_kb);
            } else if (strncmp(buffer, "MemFree:", 8) == 0) {
                sscanf(buffer, "MemFree: %lu", &metrics.free_mem_kb);
            } else if (strncmp(buffer, "MemAvailable:", 13) == 0) {
                sscanf(buffer, "MemAvailable: %lu", &metrics.avail_mem_kb);
                break; // Got everything we need for the RAM frame layout
            }
        }
        fclose(mem_file);
    }

    // 2. PARSE CPU DATA VIA /proc/stat
    // To get real-time CPU usage, we take a snapshot, pause briefly, and take another to calculate differentials
    unsigned long long user1, nice1, system1, idle1, iowait1, irq1, softirq1;
    unsigned long long user2, nice2, system2, idle2, iowait2, irq2, softirq2;

    FILE* stat_file = fopen("/proc/stat", "r");
    if (stat_file) {
        if (fgets(buffer, sizeof(buffer), stat_file)) {
            sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu", &user1, &nice1, &system1, &idle1, &iowait1, &irq1, &softirq1);
        }
        fclose(stat_file);
    }

    // High-resolution interval pause to capture processing tick shifts
    usleep(50000); // 50 milliseconds sample delta

    stat_file = fopen("/proc/stat", "r");
    if (stat_file) {
        if (fgets(buffer, sizeof(buffer), stat_file)) {
            sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu", &user2, &nice2, &system2, &idle2, &iowait2, &irq2, &softirq2);
        }
        fclose(stat_file);
    }

    unsigned long long total1 = user1 + nice1 + system1 + idle1 + iowait1 + irq1 + softirq1;
    unsigned long long total2 = user2 + nice2 + system2 + idle2 + iowait2 + irq2 + softirq2;
    
    unsigned long long total_delta = total2 - total1;
    unsigned long long idle_delta = idle2 - idle1;

    if (total_delta > 0) {
        metrics.cpu_usage_percentage = ((double)(total_delta - idle_delta) / total_delta) * 100.0;
    }

    return metrics;
}