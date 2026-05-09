#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <string_view>
#include <functional>
#include <mutex>
#include <deque>
#include <memory>

#include "protocol.h"
#include "ota.h"
#include "device_state_event.h"


#define MAIN_EVENT_SCHEDULE (1 << 0)
#define MAIN_EVENT_ERROR (1 << 4)
#define MAIN_EVENT_CLOCK_TICK (1 << 6)
#define MAIN_START_OTA (1 << 7)

class VoiceController;

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Start();
    void MainEventLoop();
    bool IsMainEventLoopTask() const { return xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_; }
    DeviceState GetDeviceState() const { return device_state_; }
    void Schedule(std::function<void()> callback);
    void EnterWifiConfigMode();
    void EnterRunning();
    void ReportError(const std::string& message);
    void RequestReboot();
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();
    void StartFirmwareUpgrade(const std::string &url, const std::string &version = "");
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetServerTimeSynced(bool synced = true);

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    std::string last_error_message_;

    bool has_server_time_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t main_event_loop_task_handle_ = nullptr;
    TaskHandle_t esphome_loop_task_handle_ = nullptr;

    std::string _ota_url;
    std::string _ota_version;

    void SetDeviceState(DeviceState state);
    void Reboot();
    bool UpgradeFirmware(Ota& ota, const std::string& url = "", const std::string& version = "");
    void startOtaUpgrade(const std::string& url, const std::string& version);
    bool otaUpgrade();
    void CheckAssetsVersion();
    void PlayFeedbackSound(const std::string_view& sound);
};


class TaskPriorityReset {
public:
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
