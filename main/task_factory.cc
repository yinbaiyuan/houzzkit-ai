#include "task_factory.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#define TAG "TaskFactory"

namespace {

bool IsExternalStackEnabled() {
#if defined(CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY && \
    defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM) && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    return true;
#else
    return false;
#endif
}

bool AllocateExternalTaskMemory(const char* name,
                                uint32_t stack_depth,
                                StackType_t** stack,
                                StaticTask_t** task_buffer) {
    if (!IsExternalStackEnabled()) {
        ESP_LOGW(TAG, "External task stack is disabled, fallback to internal stack for %s", name);
        return false;
    }

    *stack = static_cast<StackType_t*>(heap_caps_malloc(stack_depth, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (*stack == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate %lu bytes PSRAM stack for %s, fallback to internal stack",
                 static_cast<unsigned long>(stack_depth), name);
        return false;
    }

    *task_buffer = static_cast<StaticTask_t*>(
        heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (*task_buffer == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate task control block for %s, fallback to internal stack", name);
        heap_caps_free(*stack);
        *stack = nullptr;
        return false;
    }

    return true;
}

BaseType_t FallbackCreateTask(TaskFunction_t task_code,
                              const char* name,
                              const uint32_t stack_depth,
                              void* parameters,
                              UBaseType_t priority,
                              TaskHandle_t* created_task) {
    return xTaskCreate(task_code, name, stack_depth, parameters, priority, created_task);
}

BaseType_t FallbackCreatePinnedTask(TaskFunction_t task_code,
                                    const char* name,
                                    const uint32_t stack_depth,
                                    void* parameters,
                                    UBaseType_t priority,
                                    TaskHandle_t* created_task,
                                    const BaseType_t core_id) {
    return xTaskCreatePinnedToCore(task_code, name, stack_depth, parameters, priority, created_task, core_id);
}

} // namespace

BaseType_t CreateTaskWithExternalStack(TaskFunction_t task_code,
                                       const char* name,
                                       const uint32_t stack_depth,
                                       void* parameters,
                                       UBaseType_t priority,
                                       TaskHandle_t* created_task) {
    StackType_t* stack = nullptr;
    StaticTask_t* task_buffer = nullptr;
    if (!AllocateExternalTaskMemory(name, stack_depth, &stack, &task_buffer)) {
        return FallbackCreateTask(task_code, name, stack_depth, parameters, priority, created_task);
    }

    TaskHandle_t handle = xTaskCreateStatic(task_code, name, stack_depth, parameters, priority, stack, task_buffer);
    if (handle == nullptr) {
        ESP_LOGW(TAG, "Failed to create %s with PSRAM stack, fallback to internal stack", name);
        heap_caps_free(task_buffer);
        heap_caps_free(stack);
        return FallbackCreateTask(task_code, name, stack_depth, parameters, priority, created_task);
    }

    if (created_task != nullptr) {
        *created_task = handle;
    }
    ESP_LOGI(TAG, "Created %s with %lu bytes PSRAM stack", name, static_cast<unsigned long>(stack_depth));
    return pdPASS;
}

BaseType_t CreatePinnedTaskWithExternalStack(TaskFunction_t task_code,
                                             const char* name,
                                             const uint32_t stack_depth,
                                             void* parameters,
                                             UBaseType_t priority,
                                             TaskHandle_t* created_task,
                                             const BaseType_t core_id) {
    StackType_t* stack = nullptr;
    StaticTask_t* task_buffer = nullptr;
    if (!AllocateExternalTaskMemory(name, stack_depth, &stack, &task_buffer)) {
        return FallbackCreatePinnedTask(task_code, name, stack_depth, parameters, priority, created_task, core_id);
    }

    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(task_code, name, stack_depth, parameters, priority, stack,
                                                        task_buffer, core_id);
    if (handle == nullptr) {
        ESP_LOGW(TAG, "Failed to create %s with PSRAM stack, fallback to internal stack", name);
        heap_caps_free(task_buffer);
        heap_caps_free(stack);
        return FallbackCreatePinnedTask(task_code, name, stack_depth, parameters, priority, created_task, core_id);
    }

    if (created_task != nullptr) {
        *created_task = handle;
    }
    ESP_LOGI(TAG, "Created %s with %lu bytes PSRAM stack on core %ld", name,
             static_cast<unsigned long>(stack_depth), static_cast<long>(core_id));
    return pdPASS;
}
