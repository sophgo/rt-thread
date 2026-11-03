/* syslog.h — RT-Thread stub for TDL SDK compatibility */
#pragma once

/* RT-Thread doesn't have syslog, use printf instead */
#ifndef syslog
#define syslog(level, fmt, ...) printf(fmt, ##__VA_ARGS__)
#endif

/* Log levels (unused in stub) */
#define LOG_DEBUG   7
#define LOG_INFO    6
#define LOG_NOTICE  5
#define LOG_WARNING 4
#define LOG_ERR     3
#define LOG_CRIT    2
