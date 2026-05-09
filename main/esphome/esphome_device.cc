#include "esphome_device.h"
#include "esphome.h"
#include "ble_manager.h"
#include "board.h"
#include "display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "settings.h"
#include "system_info.h"

#include <esp_app_desc.h>
#include <wifi_station.h>

#define TAG "ESPHomeDevice"

esphome::api::APIServer *api_apiserver_id = nullptr;
esphome::preferences::IntervalSyncer *preferences_intervalsyncer_id;
esphome::sensor::Sensor *ota_download_progress_sensor_id;
esphome::text_sensor::TextSensor *device_ip_sensor_id;
esphome::text_sensor::TextSensor *device_mac_sensor_id;
esphome::text_sensor::TextSensor *current_version_sensor_id;

class FirmwareUpgradeButton : public esphome::button::Button
{
public:
  void press_action() override
  {
    auto &device = ESPHomeDevice::GetInstance();
    Application::GetInstance().StartFirmwareUpgrade(device.otaUpgradeUrl(), device.latestVersion());
  };
};

FirmwareUpgradeButton *firmware_upgrade_button_id;


class OtaUpgradeUrlText : public esphome::text::Text
{
public:
  void control(const std::string &value) override
  {
    ESPHomeDevice::GetInstance().setOtaUpgradeUrl(value);
  };
};

OtaUpgradeUrlText *ota_upgrade_url_text_id;

class LatestVersionText : public esphome::text::Text
{
public:
  void control(const std::string &value) override
  {
    ESPHomeDevice::GetInstance().setLatestVersion(value);
  };
};

LatestVersionText *latest_version_text_id;


ESPHomeDevice &ESPHomeDevice::GetInstance()
{
  static ESPHomeDevice instance;
  return instance;
}

ESPHomeDevice::ESPHomeDevice()
{
}

ESPHomeDevice::~ESPHomeDevice()
{
}

void ESPHomeDevice::setupPreferences()
{
  esphome::esp32::setup_preferences();

  Settings settings("esphome", false);
  _idleScreenOff = settings.GetBool("iSOff", _idleScreenOff);
}


void ESPHomeDevice::setup()
{
  auto &board = Board::GetInstance();
  std::string device_name = board.getDeviceName();
  esphome::App.pre_setup(device_name, device_name, "", "", __DATE__ ", " __TIME__, false);

  // 预留组件内存空间
  esphome::App.reserve_components(7);


#if !CONFIG_IDF_TARGET_ESP32P4
  api_apiserver_id = new esphome::api::APIServer();
  api_apiserver_id->set_component_source("api");
  esphome::App.register_component(api_apiserver_id);
  api_apiserver_id->set_port(6053);
  api_apiserver_id->set_password("");
  api_apiserver_id->set_reboot_timeout(0);
  api_apiserver_id->set_batch_delay(100);
#else
  ESP_LOGW(TAG, "Temporarily disable ESPHome API server on ESP32-P4 to verify random reboot issue");
#endif

  preferences_intervalsyncer_id = new esphome::preferences::IntervalSyncer();
  preferences_intervalsyncer_id->set_write_interval(60000);
  preferences_intervalsyncer_id->set_component_source("preferences");
  esphome::App.register_component(preferences_intervalsyncer_id);


  // 注册固件升级按钮
  firmware_upgrade_button_id = new FirmwareUpgradeButton();
  esphome::App.register_button(firmware_upgrade_button_id);
  firmware_upgrade_button_id->set_name(Lang::Strings::ESPHOME_ENTITY_BUTTON_NAME_FIRMWARE_UPGRADE);
  firmware_upgrade_button_id->set_object_id("firmware_upgrade_button");
  firmware_upgrade_button_id->set_disabled_by_default(false);
  firmware_upgrade_button_id->set_entity_category(esphome::ENTITY_CATEGORY_CONFIG);
  firmware_upgrade_button_id->set_icon("mdi:update");


  current_version_sensor_id = new esphome::text_sensor::TextSensor();
  esphome::App.register_text_sensor(current_version_sensor_id);
  current_version_sensor_id->set_name(Lang::Strings::ESPHOME_ENTITY_SENSOR_NAME_CURRENT_VERSION);
  current_version_sensor_id->set_object_id("current_version");
  current_version_sensor_id->set_disabled_by_default(false);
  current_version_sensor_id->set_entity_category(esphome::ENTITY_CATEGORY_DIAGNOSTIC);
  current_version_sensor_id->publish_state(esp_app_get_description()->version);

  // 注册设备 IP
  device_ip_sensor_id = new esphome::text_sensor::TextSensor();
  esphome::App.register_text_sensor(device_ip_sensor_id);
  device_ip_sensor_id->set_name(Lang::Strings::ESPHOME_ENTITY_SENSOR_NAME_DEVICE_IP);
  device_ip_sensor_id->set_object_id("device_ip");
  device_ip_sensor_id->set_disabled_by_default(false);
  device_ip_sensor_id->set_entity_category(esphome::ENTITY_CATEGORY_DIAGNOSTIC);
  this->updateDeviceIp();

  // 注册设备 MAC
  device_mac_sensor_id = new esphome::text_sensor::TextSensor();
  esphome::App.register_text_sensor(device_mac_sensor_id);
  device_mac_sensor_id->set_name(Lang::Strings::ESPHOME_ENTITY_SENSOR_NAME_DEVICE_MAC);
  device_mac_sensor_id->set_object_id("device_mac");
  device_mac_sensor_id->set_disabled_by_default(false);
  device_mac_sensor_id->set_entity_category(esphome::ENTITY_CATEGORY_DIAGNOSTIC);
  device_mac_sensor_id->publish_state(SystemInfo::GetMacAddress());

  // 注册 OTA 下载进度
  ota_download_progress_sensor_id = new esphome::sensor::Sensor();
  esphome::App.register_sensor(ota_download_progress_sensor_id);
  ota_download_progress_sensor_id->set_name(Lang::Strings::ESPHOME_ENTITY_SENSOR_NAME_OTA_DOWNLOAD_PROGRESS);
  ota_download_progress_sensor_id->set_object_id("ota_download_progress");
  ota_download_progress_sensor_id->set_disabled_by_default(false);
  ota_download_progress_sensor_id->set_entity_category(esphome::ENTITY_CATEGORY_DIAGNOSTIC);
  ota_download_progress_sensor_id->set_unit_of_measurement("%");
  ota_download_progress_sensor_id->set_accuracy_decimals(0);
  ota_download_progress_sensor_id->set_state_class(esphome::sensor::STATE_CLASS_MEASUREMENT);
  ota_download_progress_sensor_id->publish_state(_otaDownloadProgress);

  // 注册 OTA 升级地址
  ota_upgrade_url_text_id = new OtaUpgradeUrlText();
  esphome::App.register_text(ota_upgrade_url_text_id);
  ota_upgrade_url_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_OTA_UPGRADE_URL);
  ota_upgrade_url_text_id->set_object_id("ota_upgrade_url");
  ota_upgrade_url_text_id->set_disabled_by_default(false);
  ota_upgrade_url_text_id->set_entity_category(esphome::ENTITY_CATEGORY_CONFIG);
  ota_upgrade_url_text_id->set_icon("mdi:link");
  ota_upgrade_url_text_id->traits.set_min_length(0);
  ota_upgrade_url_text_id->traits.set_max_length(512);
  ota_upgrade_url_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  ota_upgrade_url_text_id->publish_state(_otaUpgradeUrl);

  // 注册最新版本号
  latest_version_text_id = new LatestVersionText();
  esphome::App.register_text(latest_version_text_id);
  latest_version_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_LATEST_VERSION);
  latest_version_text_id->set_object_id("latest_version");
  latest_version_text_id->set_disabled_by_default(false);
  latest_version_text_id->set_entity_category(esphome::ENTITY_CATEGORY_CONFIG);
  latest_version_text_id->set_icon("mdi:tag");
  latest_version_text_id->traits.set_min_length(0);
  latest_version_text_id->traits.set_max_length(32);
  latest_version_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  latest_version_text_id->publish_state(_latestVersion);


  board.RegisterESPHomeEntities(*this);

  esphome::App.setup();
}

void ESPHomeDevice::loop()
{
  esphome::App.loop();
  updateDeviceIp();
}

void ESPHomeDevice::setNoisePsk(const std::string noise_psk)
{
  if (api_apiserver_id == nullptr)
  {
    ESP_LOGW(TAG, "ESPHome API server is unavailable, skip setting noise_psk");
    return;
  }

  esphome::api::psk_t psk;
  if (noise_psk.length() != 64)
  {
    ESP_LOGE(TAG, "Invalid noise_psk length, must be 64 characters");
    return;
  }
  for (int i = 0; i < 32; i++)
  {
    psk[i] = std::stoi(noise_psk.substr(i * 2, 2), nullptr, 16);
  }
  api_apiserver_id->save_noise_psk(psk, true);
}

void ESPHomeDevice::setIdleScreenOff(bool enabled)
{
  _idleScreenOff = enabled;
  Settings settings("esphome", true);
  settings.SetBool("iSOff", _idleScreenOff);
  BLEManager::GetInstance().notifyIdleScreenOff(_idleScreenOff);
  if (Application::GetInstance().GetDeviceState() == kDeviceStateRunning)
  {
      auto &board = Board::GetInstance();
      auto display = board.GetDisplay();
      if (display != nullptr) {
          display->setDisplayOnOff(!enabled);
      }
  }
}


void ESPHomeDevice::setOtaUpgradeUrl(const std::string &value)
{
  _otaUpgradeUrl = value;
  if (ota_upgrade_url_text_id != nullptr)
  {
    ota_upgrade_url_text_id->publish_state(_otaUpgradeUrl);
  }
}

void ESPHomeDevice::setLatestVersion(const std::string &value)
{
  _latestVersion = value;
  if (latest_version_text_id != nullptr)
  {
    latest_version_text_id->publish_state(_latestVersion);
  }
}


void ESPHomeDevice::setOtaDownloadProgress(uint8_t progress)
{
  _otaDownloadProgress = progress > 100 ? 100 : progress;
  if (ota_download_progress_sensor_id != nullptr)
  {
    ota_download_progress_sensor_id->publish_state(_otaDownloadProgress);
  }
}


void ESPHomeDevice::updateDeviceIp()
{
  std::string ip = WifiStation::GetInstance().GetIpAddress();
  if (ip == _deviceIp && device_ip_sensor_id != nullptr && device_ip_sensor_id->has_state())
  {
    return;
  }
  _deviceIp = ip;
  if (device_ip_sensor_id != nullptr)
  {
    device_ip_sensor_id->publish_state(_deviceIp);
  }
}
