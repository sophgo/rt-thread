#include <rtthread.h>
#include <string.h>

#ifdef RT_USING_FINSH
#include <finsh.h>

/* Configuration Options */
#define RT_PERF_THREAD_NUM_MAX RT_THREAD_PRIORITY_MAX

/* Structure to store performance data for each thread */
typedef struct {
    rt_uint64_t
        total_ticks;  // Total running time of the thread (provided by a high-precision clock)
    rt_uint64_t start_time;    // Timestamp when the thread starts running
    rt_uint32_t switch_count;  // Number of times the thread has been scheduled
    const char *thread_name;   // Thread name
    rt_thread_t thread_ptr;    // Thread handle
} rt_perf_thread_t;

/* Global Variables */
static rt_perf_thread_t perf_stat[RT_PERF_THREAD_NUM_MAX];
static rt_uint64_t      total_system_ticks = 0;
static rt_uint64_t      perf_start_time    = 0;
static rt_bool_t        is_perf_inited     = RT_FALSE;

static rt_uint64_t rt_perf_get_ticks(void)
{
#if defined(ARCH_RISCV)
    // For RISC-V series, mcycle counter can be used
    rt_uint64_t tick;
    __asm__ __volatile__("rdtime %0" : "=r"(tick));
    return tick;
#else
    // For other platforms, please return the count value of a high-precision timer
    // If not available, rt_tick_get_us() can be used temporarily, but with limited precision
    return rt_tick_get_us();
#endif
}

/*
 * Scheduler hook function, called by the kernel on every thread switch
 */
static void perf_scheduler_hook(struct rt_thread *from, struct rt_thread *to)
{
    rt_uint64_t now_ticks;

    if (!is_perf_inited) return;

    now_ticks = rt_perf_get_ticks();

    /* Calculate the execution time of the thread being switched out */
    if (from != RT_NULL && from->user_data < RT_PERF_THREAD_NUM_MAX) {
        rt_uint32_t idx = from->user_data;
        if (perf_stat[idx].thread_ptr == from)  // Ensure data is valid
        {
            perf_stat[idx].total_ticks += now_ticks - perf_stat[idx].start_time;
        }
    }

    /* Record the start time of the thread being switched in */
    if (to != RT_NULL && to->user_data < RT_PERF_THREAD_NUM_MAX) {
        rt_uint32_t idx = to->user_data;
        if (perf_stat[idx].thread_ptr == to)  // Ensure data is valid
        {
            perf_stat[idx].start_time = now_ticks;
            perf_stat[idx].switch_count++;
        }
    }
}

/*
 * MSH Command: Initialize and start performance statistics
 */
static void perf_init(void)
{
    if (is_perf_inited) {
        rt_kprintf("Performance utility is already running.\n");
        return;
    }

    rt_kprintf("Initializing performance utility...\n");

    // Initialize all statistics structures
    rt_memset(perf_stat, 0, sizeof(perf_stat));

    // Iterate through all threads and assign a statistics slot for each
    struct rt_object_information *info;
    struct rt_list_node          *node;
    info = rt_object_get_information(RT_Object_Class_Thread);
    if (!info) {
        rt_kprintf("Error: Failed to get thread info. This feature might be disabled in kernel.\n");
        return;
    }
    rt_enter_critical();
    node          = info->object_list.next;
    rt_uint32_t i = 0;
    while (node != &(info->object_list) && i < RT_PERF_THREAD_NUM_MAX) {
        struct rt_object *obj    = rt_list_entry(node, struct rt_object, list);
        rt_thread_t       thread = (rt_thread_t)obj;

        thread->user_data        = i;  // Use user_data to store the index
        perf_stat[i].thread_ptr  = thread;
        perf_stat[i].thread_name = obj->name;
        node                     = node->next;
        i++;
    }
    rt_exit_critical();
    // Record the global start time
    perf_start_time = rt_perf_get_ticks();
    // Also record the start time of the current thread (usually tshell)
    rt_thread_t self = rt_thread_self();
    if (self != RT_NULL && self->user_data < RT_PERF_THREAD_NUM_MAX) {
        perf_stat[self->user_data].start_time = perf_start_time;
    }

    is_perf_inited = RT_TRUE;
    // Set the scheduler hook
    rt_scheduler_sethook(perf_scheduler_hook);

    rt_kprintf("Performance utility started.\n");
}
MSH_CMD_EXPORT(perf_init, Initialize the performance utility);

/*
 * MSH Command: Display performance statistics results
 */
static void perf_show_stat(void)
{
    if (!is_perf_inited) {
        rt_kprintf("Performance utility is not running. Please run 'perf_init' first.\n");
        return;
    }

    // Pause the hook to prevent race conditions while reading data
    rt_scheduler_sethook(RT_NULL);

    total_system_ticks = rt_perf_get_ticks() - perf_start_time;

    // Find the running time of the idle thread to calculate total CPU usage
    rt_uint64_t idle_ticks = 0;
    for (int i = 0; i < RT_PERF_THREAD_NUM_MAX; i++) {
        if (perf_stat[i].thread_ptr != RT_NULL && strcmp(perf_stat[i].thread_name, "tidle0") == 0) {
            idle_ticks = perf_stat[i].total_ticks;
            break;
        }
    }

    rt_kprintf(
        "============================================================================\n");

    if (total_system_ticks > 0) {
        rt_uint64_t busy_ticks   = total_system_ticks - idle_ticks;
        rt_uint64_t usage_scaled = (busy_ticks * 10000) / total_system_ticks;
        rt_uint32_t usage_major  = usage_scaled / 100;
        rt_uint32_t usage_minor  = usage_scaled % 100;
        rt_kprintf(" Total CPU Usage: %3u.%02u%%\n", usage_major, usage_minor);
    }

    rt_kprintf(
        "============================================================================\n");
    rt_kprintf(" Thread Perf Stat (Total %llu ticks)\n", total_system_ticks);
    rt_kprintf(
        "----------------------------------------------------------------------------\n");
    rt_kprintf(
        " Thread Name      | Prio | Stack Size | Total Ticks  | Switches | CPU Usage \n");
    rt_kprintf(
        "------------------|------|------------|--------------|----------|-----------\n");

    for (int i = 0; i < RT_PERF_THREAD_NUM_MAX; i++) {
        if (perf_stat[i].thread_ptr != RT_NULL) {
            rt_uint32_t usage_major = 0;
            rt_uint32_t usage_minor = 0;
            rt_thread_t thread      = perf_stat[i].thread_ptr;
            if (total_system_ticks > 0) {
                rt_uint64_t usage_scaled = (perf_stat[i].total_ticks * 10000) / total_system_ticks;
                usage_major              = usage_scaled / 100;
                usage_minor              = usage_scaled % 100;
            }
            rt_kprintf(" %-16s | %4d | %10u | %-12llu | %-8u | %3u.%02u%%\n",
                       perf_stat[i].thread_name, RT_SCHED_PRIV(thread).current_priority,
                       thread->stack_size, perf_stat[i].total_ticks, perf_stat[i].switch_count,
                       usage_major, usage_minor);
        }
    }
    rt_kprintf(
        "============================================================================\n");

    // Restart the hook
    rt_scheduler_sethook(perf_scheduler_hook);
}
MSH_CMD_EXPORT(perf_show_stat, Show thread performance statistics);

#endif /* RT_USING_FINSH */