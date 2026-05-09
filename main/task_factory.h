#ifndef _TASK_FACTORY_H_
#define _TASK_FACTORY_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// For long-lived tasks only. Static stacks are not reclaimed when a task exits.
BaseType_t CreateTaskWithExternalStack(TaskFunction_t task_code,
                                       const char* name,
                                       const uint32_t stack_depth,
                                       void* parameters,
                                       UBaseType_t priority,
                                       TaskHandle_t* created_task);

BaseType_t CreatePinnedTaskWithExternalStack(TaskFunction_t task_code,
                                             const char* name,
                                             const uint32_t stack_depth,
                                             void* parameters,
                                             UBaseType_t priority,
                                             TaskHandle_t* created_task,
                                             const BaseType_t core_id);

#endif // _TASK_FACTORY_H_
