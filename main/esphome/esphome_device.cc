#include "esphome_device.h"
#include "esphome.h"
#include "ble_manager.h"
#include "board.h"
#include "display.h"
#include "application.h"
#include "assets/lang_config.h"
#include "settings.h"

#define TAG "ESPHomeDevice"

esphome::api::APIServer *api_apiserver_id;
esphome::preferences::IntervalSyncer *preferences_intervalsyncer_id;

class WakeupButton : public esphome::button::Button
{
public:
  void press_action() override
  {
    Application::GetInstance().ToggleChatState();
  };
};

WakeupButton *wakeup_button_id;

class MicSwitch : public esphome::switch_::Switch
{
public:
  void write_state(bool state) override
  {
    ESPHomeDevice::GetInstance().setMicEnable(state);
  };
};

MicSwitch *mic_switch_id;

class VolumeNumber : public esphome::number::Number
{
public:
  void control(float value) override
  {
    ESPHomeDevice::GetInstance().setOutputVolume(value);
  };
};

VolumeNumber *volume_number_id;

class PlayVoiceText : public esphome::text::Text
{
public:
  void control(const std::string &value) override
  {
    ESPHomeDevice::GetInstance().setPlayVoiceText(value);
  };
};

PlayVoiceText *play_voice_text_id;

class ExecuteCommandText : public esphome::text::Text
{
public:
  void control(const std::string &value) override
  {
    ESPHomeDevice::GetInstance().setExecuteCommandText(value);
  };
};

ExecuteCommandText *execute_command_text_id;



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
  _micEnabled = settings.GetBool("micEnabled", _micEnabled);
  _continuousDialogue = settings.GetBool("cDialogue", _continuousDialogue);
  _voiceResponseSound = false;//settings.GetBool("vrSound", _voiceResponseSound);//暂不开放
  _idleScreenOff = settings.GetBool("iSOff", _idleScreenOff);
}

void ESPHomeDevice::initProperties()
{
  auto &board = Board::GetInstance();
  auto codec = board.GetAudioCodec();
  _outputVolume = codec->output_volume();
  codec->EnableInput(_micEnabled);
}

void ESPHomeDevice::setup()
{
  auto &board = Board::GetInstance();
  std::string device_name = board.getDeviceName();
  esphome::App.pre_setup(device_name, device_name, "", "", __DATE__ ", " __TIME__, false);

  // 预留组件内存空间
  esphome::App.reserve_components(6);

  initProperties();

  api_apiserver_id = new esphome::api::APIServer();
  api_apiserver_id->set_component_source("api");
  esphome::App.register_component(api_apiserver_id);
  api_apiserver_id->set_port(6053);
  api_apiserver_id->set_password("");
  api_apiserver_id->set_reboot_timeout(0);
  api_apiserver_id->set_batch_delay(100);

  preferences_intervalsyncer_id = new esphome::preferences::IntervalSyncer();
  preferences_intervalsyncer_id->set_write_interval(60000);
  preferences_intervalsyncer_id->set_component_source("preferences");
  esphome::App.register_component(preferences_intervalsyncer_id);

  // 注册音箱唤醒按钮
  wakeup_button_id = new WakeupButton();
  esphome::App.register_button(wakeup_button_id);
  wakeup_button_id->set_name(Lang::Strings::ESPHOME_ENTITY_BUTTON_NAME_WAKEUP);
  wakeup_button_id->set_object_id("wakeup_button");
  wakeup_button_id->set_disabled_by_default(false);

  // 注册麦克风开关
  mic_switch_id = new MicSwitch();
  esphome::App.register_switch(mic_switch_id);
  mic_switch_id->set_name(Lang::Strings::ESPHOME_ENTITY_SWITCH_NAME_MIC_ENABLE);
  mic_switch_id->set_object_id("mic_switch");
  mic_switch_id->set_disabled_by_default(false);
  mic_switch_id->publish_state(this->micEnabled());

  // 注册音量调节
  volume_number_id = new VolumeNumber();
  esphome::App.register_number(volume_number_id);
  volume_number_id->set_name(Lang::Strings::ESPHOME_ENTITY_NUMBER_NAME_VOLUME);
  volume_number_id->set_object_id("volume_number");
  volume_number_id->set_disabled_by_default(false);
  volume_number_id->traits.set_min_value(0.0f);
  volume_number_id->traits.set_max_value(100.0f);
  volume_number_id->traits.set_step(1.0f);
  volume_number_id->traits.set_mode(esphome::number::NUMBER_MODE_SLIDER);
  volume_number_id->publish_state(this->outputVolume());

  play_voice_text_id = new PlayVoiceText();
  esphome::App.register_text(play_voice_text_id);
  play_voice_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_PLAY_VOICE);
  play_voice_text_id->set_object_id("play_voice_text");
  play_voice_text_id->set_disabled_by_default(false);
  play_voice_text_id->traits.set_min_length(0);
  play_voice_text_id->traits.set_max_length(50);
  play_voice_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  play_voice_text_id->publish_state("");

  execute_command_text_id = new ExecuteCommandText();
  esphome::App.register_text(execute_command_text_id);
  execute_command_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_EXECUTE_COMMAND);
  execute_command_text_id->set_object_id("execute_command_text");
  execute_command_text_id->set_disabled_by_default(false);
  execute_command_text_id->traits.set_min_length(0);
  execute_command_text_id->traits.set_max_length(50);
  execute_command_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  execute_command_text_id->publish_state("");

  esphome::App.setup();
}

void ESPHomeDevice::loop()
{
  esphome::App.loop();
}

void ESPHomeDevice::setNoisePsk(const std::string noise_psk)
{
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

void ESPHomeDevice::setOutputVolume(uint8_t volume)
{
  _outputVolume = volume;
  volume_number_id->publish_state(volume);
  auto &board = Board::GetInstance();
  auto codec = board.GetAudioCodec();
  codec->SetOutputVolume(volume);
  BLEManager::GetInstance().notifyVolume(volume);
  ESP_LOGI(TAG, "Set output volume to %d", volume);
}

void ESPHomeDevice::setMicEnable(bool enabled)
{
  _micEnabled = enabled;
  Settings settings("esphome", true);
  settings.SetBool("micEnabled", _micEnabled);
  mic_switch_id->publish_state(_micEnabled);
  BLEManager::GetInstance().notifyMicSwitchState(_micEnabled);
  ESP_LOGI(TAG, "Set mic enabled to %d", _micEnabled);

  // auto &audioService = Application::GetInstance().GetAudioService();
  //     ESP_LOGI(TAG, "micSwitch state: %s", state ? "ON" : "OFF");
  //     audioService.EnableWakeWordDetection(state);
}

void ESPHomeDevice::setContinuousDialogue(bool enabled)
{
  _continuousDialogue = enabled;
  Settings settings("esphome", true);
  settings.SetBool("cDialogue", _continuousDialogue);
  BLEManager::GetInstance().notifyContinuousDialogue(_continuousDialogue);
}

void ESPHomeDevice::setVoiceResponseSound(bool enabled)
{
  _voiceResponseSound = enabled;
  Settings settings("esphome", true);
  settings.SetBool("vrSound", _voiceResponseSound);
  BLEManager::GetInstance().notifyVoiceResponseSound(_voiceResponseSound);
}

void ESPHomeDevice::setIdleScreenOff(bool enabled)
{
  _idleScreenOff = enabled;
  Settings settings("esphome", true);
  settings.SetBool("iSOff", _idleScreenOff);
  BLEManager::GetInstance().notifyIdleScreenOff(_idleScreenOff);
  if (Application::GetInstance().GetDeviceState() == DeviceState::kDeviceStateIdle)
  {
      auto &board = Board::GetInstance();
      auto display = board.GetDisplay();
      display->setDisplayOnOff(!enabled);
  }

}

void ESPHomeDevice::setPlayVoiceText(const std::string &value)
{
  printf("PlayVoiceText: %s\n", value.c_str());
  Application::GetInstance().playVoiceText(value);
  play_voice_text_id->publish_state("");
}

void ESPHomeDevice::setExecuteCommandText(const std::string &value)
{
  printf("ExecuteCommandText: %s\n", value.c_str());
  Application::GetInstance().executeCommandText(value);
  execute_command_text_id->publish_state("");
}