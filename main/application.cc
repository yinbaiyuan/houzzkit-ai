#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"

#if CONFIG_USE_VOICE_DIALOGUE
#include "voice_controller.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#include "esphome_device.h"
#include "ble_manager.h"
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_partition.h>

#define TAG "Application"

static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "running",
    "upgrading",
    "activating",
    "fatal_error",
    "invalid_state"
};

namespace {

void MarkCurrentFirmwareValid() {
    auto partition = esp_ota_get_running_partition();
    if (partition == nullptr) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return;
    }

    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping firmware validation");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

}

Application::Application() {
    event_group_ = xEventGroupCreate();

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }

    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    PlayFeedbackSound(sound);
}

void Application::PlayFeedbackSound(const std::string_view& sound) {
#if CONFIG_USE_VOICE_DIALOGUE
    if (!sound.empty()) {
        Board::GetInstance().GetVoiceController()->PlaySound(sound);
    }
#else
    (void)sound;
#endif
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateRunning) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::Start() {

    auto& board = Board::GetInstance();
    board.setDeviceNamePrefix("HOUZZkit");

    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();
    display->setBacklight(board.GetBacklight());

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

#if CONFIG_USE_VOICE_DIALOGUE
    board.GetVoiceController()->Initialize(*this, event_group_);
#endif

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    MarkCurrentFirmwareValid();

    // Check for new assets version
    CheckAssetsVersion();

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    Settings mqtt_settings("mqtt", false);
    Settings websocket_settings("websocket", false);
    auto mqtt_endpoint = mqtt_settings.GetString("endpoint");
    auto websocket_url = websocket_settings.GetString("ws_url");

    if (!mqtt_endpoint.empty()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (!websocket_url.empty()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol config found, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        ReportError(message);
    });
#if CONFIG_USE_VOICE_DIALOGUE
    board.GetVoiceController()->SetupProtocol(*protocol_);
#endif
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
#if CONFIG_USE_VOICE_DIALOGUE
        if (Board::GetInstance().GetVoiceController()->HandleIncomingJson(root, display)) {
            return;
        }
#endif
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Incoming message requires string type");
            return;
        }

        if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    RequestReboot();
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateRunning);

    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + esp_app_get_description()->version;
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        PlayFeedbackSound(Lang::Sounds::OGG_SUCCESS);
    }

    /* Start BLE */
    BLEManager::GetInstance().start(board.getDeviceName());

#if CONFIG_IDF_TARGET_ESP32P4
    xTaskCreatePinnedToCore([](void* arg) {
        ESPHomeDevice& esphomeDevice = ESPHomeDevice::GetInstance();
        esphomeDevice.setup();
        while (true)
        {
            esphomeDevice.loop();
        }
        vTaskDelete(NULL);
    }, "esphome_loop", 2048 * 4, nullptr, 2, &esphome_loop_task_handle_, 0);
#else
    xTaskCreate([](void* arg) {
        ESPHomeDevice& esphomeDevice = ESPHomeDevice::GetInstance();
        esphomeDevice.setup();
        while (true)
        {
            esphomeDevice.loop();
        }
        vTaskDelete(NULL);
    }, "esphome_loop", 2048 * 4, nullptr, 4, &esphome_loop_task_handle_);
#endif

}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::EnterWifiConfigMode() {
    if (IsMainEventLoopTask()) {
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    Schedule([this]() {
        SetDeviceState(kDeviceStateWifiConfiguring);
    });
}

void Application::EnterRunning() {
    if (IsMainEventLoopTask()) {
        SetDeviceState(kDeviceStateRunning);
        return;
    }

    Schedule([this]() {
        SetDeviceState(kDeviceStateRunning);
    });
}

void Application::ReportError(const std::string& message) {
    last_error_message_ = message;
    xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
}

void Application::RequestReboot() {
    Schedule([this]() {
        Reboot();
    });
}

// The main event loop serializes system state transitions and scheduled work.
void Application::MainEventLoop()
{
    while (true)
    {
        EventBits_t event_mask = MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR |
            MAIN_START_OTA;
#if CONFIG_USE_VOICE_DIALOGUE
        auto voice = Board::GetInstance().GetVoiceController();
        event_mask |= voice->GetPreScheduleEventMask() | voice->GetPostScheduleEventMask();
#endif
        auto bits = xEventGroupWaitBits(event_group_, event_mask, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_START_OTA)
        {
            auto &board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (otaUpgrade()) {
                // Upgrade success, reboot immediately
                ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");

                display->SetChatMessage("system", "Upgrade successful, rebooting...");
                vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
                Reboot();
                return; // This line will never be reached after reboot
            } else {
                // Upgrade failed, restart audio service and continue running
                ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
#if CONFIG_USE_VOICE_DIALOGUE
                Board::GetInstance().GetVoiceController()->RecoverAfterFirmwareUpgradeFailure();
#endif
                board.SetPowerSaveMode(true); // Restore power save mode
                Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
                vTaskDelay(pdMS_TO_TICKS(3000));
                // Continue to normal operation (don't break, just fall through)
            }
        }

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateRunning);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

#if CONFIG_USE_VOICE_DIALOGUE
        Board::GetInstance().GetVoiceController()->HandlePreScheduleEventBits(bits);
#endif

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
#if CONFIG_USE_VOICE_DIALOGUE
        Board::GetInstance().GetVoiceController()->HandlePostScheduleEventBits(bits);
#endif
    }
}

void Application::SetDeviceState(DeviceState state) {

    if (device_state_ == state) {
        return;
    }

    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state)
    {
    case kDeviceStateUnknown:
    case kDeviceStateRunning:
    {
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        if (ESPHomeDevice::GetInstance().idleScreenOff())
        {
            display->setDisplayOnOff(false);
        }
    }
    break;
    default:
        // Do nothing
        break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
#if CONFIG_USE_VOICE_DIALOGUE
    Board::GetInstance().GetVoiceController()->PrepareForReboot();
#endif

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = !version.empty() ? version : (url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)");

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    ESPHomeDevice::GetInstance().setOtaDownloadProgress(0);

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
#if CONFIG_USE_VOICE_DIALOGUE
    board.GetVoiceController()->PrepareForFirmwareUpgrade();
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        ESPHomeDevice::GetInstance().setOtaDownloadProgress(progress);
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
#if CONFIG_USE_VOICE_DIALOGUE
        board.GetVoiceController()->RecoverAfterFirmwareUpgradeFailure();
#endif
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        ESPHomeDevice::GetInstance().setOtaDownloadProgress(100);
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::StartFirmwareUpgrade(const std::string &url, const std::string &version) {
    startOtaUpgrade(url, version);
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateRunning) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

#if CONFIG_USE_VOICE_DIALOGUE
    if (!Board::GetInstance().GetVoiceController()->CanEnterSleepMode()) {
        return false;
    }
#endif

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // Make sure you are using main thread to send MCP message
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

void Application::SetServerTimeSynced(bool synced) {
    has_server_time_ = synced;
}


void Application::startOtaUpgrade(const std::string& url, const std::string& version)
{
    if (!IsMainEventLoopTask()) {
        Schedule([this, url, version]() {
            startOtaUpgrade(url, version);
        });
        return;
    }

    if (device_state_ == kDeviceStateUpgrading) {
        ESP_LOGW(TAG, "Firmware upgrade is already running");
        return;
    }

    if (url.empty()) {
        ESP_LOGW(TAG, "Firmware upgrade URL is empty");
        Alert(Lang::Strings::ERROR, Lang::Strings::OTA_UPGRADE_URL_EMPTY, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        xTaskCreate([](void *arg) {
            auto *app = static_cast<Application *>(arg);
            vTaskDelay(pdMS_TO_TICKS(5000));
            app->Schedule([app]() {
                app->DismissAlert();
            });
            vTaskDelete(NULL);
        }, "ota_url_alert", 2048, this, 3, nullptr);
        return;
    }

    _ota_url = url;
    _ota_version = version;
    ESPHomeDevice::GetInstance().setOtaDownloadProgress(0);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);

    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + _ota_version;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
#if CONFIG_USE_VOICE_DIALOGUE
    board.GetVoiceController()->PrepareForFirmwareUpgrade();
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));

    xEventGroupSetBits(event_group_, MAIN_START_OTA);
}

bool Application::otaUpgrade()
{
    std::string firmware_url = _ota_url;
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    esp_ota_handle_t update_handle = 0;
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    std::string image_header;

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http->Open("GET", firmware_url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get firmware, status code: %d", http->GetStatusCode());
        return false;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        return false;
    }

    char buffer[512];
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            return false;
        }

        // Calculate speed and progress every second
        recent_read += ret;
        total_read += ret;
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            BLEManager::GetInstance().otaProgress(progress, recent_read);
            ESPHomeDevice::GetInstance().setOtaDownloadProgress(progress);
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, recent_read / 1024);
            display->SetChatMessage("system", buffer);
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (ret == 0) {
            break;
        }

        if (!image_header_checked) {
            image_header.append(buffer, ret);
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
                ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                auto current_version = esp_app_get_description()->version;
                if (memcmp(new_app_info.version, current_version, sizeof(new_app_info.version)) == 0) {
                    ESP_LOGE(TAG, "Firmware version is the same, skipping upgrade");
                    return false;
                }

                if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle)) {
                    esp_ota_abort(update_handle);
                    ESP_LOGE(TAG, "Failed to begin OTA");
                    return false;
                }

                image_header_checked = true;
                std::string().swap(image_header);
            }
        }
        auto err = esp_ota_write(update_handle, buffer, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            return false;
        }
    }
    http->Close();
    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    ESPHomeDevice::GetInstance().setOtaDownloadProgress(100);
    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}
