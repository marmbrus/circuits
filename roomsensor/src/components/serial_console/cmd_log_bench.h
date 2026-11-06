#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Registers log_bench, which measures what a log call costs the task that makes it
void register_log_bench(void);

#ifdef __cplusplus
}
#endif
