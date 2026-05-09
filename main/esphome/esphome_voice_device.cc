#include "esphome_voice_device.h"

#include "assets/lang_config.h"
#include "audio_codec.h"
#include "ble_manager.h"
#include "board.h"
#include "esphome.h"
#include "esphome_device.h"
#include "settings.h"

#include <ctime>
#include <esp_log.h>

#define TAG "ESPHomeVoiceDevice"

#if CONFIG_USE_VOICE_DIALOGUE
namespace {

class WakeupButton : public esphome::button::Button
{
public:
  void press_action() override
  {
    Board::GetInstance().GetVoiceController()->ToggleChatState();
  };
};

WakeupButton* wakeup_button_id = nullptr;

class MicSwitch : public esphome::switch_::Switch
{
public:
  void write_state(bool state) override
  {
    ESPHomeVoiceDevice::GetInstance().setMicEnable(state);
  };
};

MicSwitch* mic_switch_id = nullptr;

class VolumeNumber : public esphome::number::Number
{
public:
  void control(float value) override
  {
    ESPHomeVoiceDevice::GetInstance().setOutputVolume(value);
  };
};

VolumeNumber* volume_number_id = nullptr;

class ContinuousDialogueSwitch : public esphome::switch_::Switch
{
public:
  void write_state(bool state) override
  {
    ESPHomeVoiceDevice::GetInstance().setContinuousDialogue(state);
  };
};

ContinuousDialogueSwitch* continuous_dialogue_switch_id = nullptr;

class SleepModeSwitch : public esphome::switch_::Switch
{
public:
  void write_state(bool state) override
  {
    ESPHomeVoiceDevice::GetInstance().setSleepMode(state);
  };
};

SleepModeSwitch* sleep_mode_switch_id = nullptr;

class SleepModeStartTime : public esphome::datetime::TimeEntity
{
public:
  void control(uint8_t hour, uint8_t minute, uint8_t second) override
  {
    (void)second;
    ESPHomeVoiceDevice::GetInstance().setSleepModeStartTime(hour, minute);
  };
};

SleepModeStartTime* sleep_mode_start_time_id = nullptr;

class SleepModeEndTime : public esphome::datetime::TimeEntity
{
public:
  void control(uint8_t hour, uint8_t minute, uint8_t second) override
  {
    (void)second;
    ESPHomeVoiceDevice::GetInstance().setSleepModeEndTime(hour, minute);
  };
};

SleepModeEndTime* sleep_mode_end_time_id = nullptr;

class PlayVoiceText : public esphome::text::Text
{
public:
  void control(const std::string& value) override
  {
    ESPHomeVoiceDevice::GetInstance().setPlayVoiceText(value);
  };
};

PlayVoiceText* play_voice_text_id = nullptr;

class ExecuteCommandText : public esphome::text::Text
{
public:
  void control(const std::string& value) override
  {
    ESPHomeVoiceDevice::GetInstance().setExecuteCommandText(value);
  };
};

ExecuteCommandText* execute_command_text_id = nullptr;

class AskAndExecuteCommandText : public esphome::text::Text
{
public:
  void control(const std::string& value) override
  {
    ESPHomeVoiceDevice::GetInstance().setAskAndExecuteCommandText(value);
  };
};

AskAndExecuteCommandText* ask_and_execute_command_text_id = nullptr;

} // namespace
#endif

ESPHomeVoiceDevice& ESPHomeVoiceDevice::GetInstance()
{
  static ESPHomeVoiceDevice instance;
  return instance;
}

ESPHomeVoiceDevice::ESPHomeVoiceDevice()
{
}

ESPHomeVoiceDevice::~ESPHomeVoiceDevice()
{
}

void ESPHomeVoiceDevice::setupPreferences()
{
#if CONFIG_USE_VOICE_DIALOGUE
  Settings settings("esphome", false);
  _micEnabled = settings.GetBool("micEnabled", _micEnabled);
  _outputVolume = settings.GetInt("volume", _outputVolume);
  _continuousDialogue = settings.GetBool("cDialogue", _continuousDialogue);
  _voiceResponseSound = false;
  _sleepMode = settings.GetBool("sleepMode", _sleepMode);
  _sleepModeTimeInterval.setSleepModeTimeInterval(settings.getUint32("sleepModeTI", _sleepModeTimeInterval.getSleepModeTimeInterval()));
#endif
}

void ESPHomeVoiceDevice::RegisterEntities(ESPHomeDevice& device)
{
  (void)device;
#if CONFIG_USE_VOICE_DIALOGUE
  auto codec = Board::GetInstance().GetAudioCodec();
  if (codec != nullptr) {
    codec->EnableInput(_micEnabled);
  }

  wakeup_button_id = new WakeupButton();
  esphome::App.register_button(wakeup_button_id);
  wakeup_button_id->set_name(Lang::Strings::ESPHOME_ENTITY_BUTTON_NAME_WAKEUP);
  wakeup_button_id->set_object_id("wakeup_button");
  wakeup_button_id->set_disabled_by_default(false);

  mic_switch_id = new MicSwitch();
  esphome::App.register_switch(mic_switch_id);
  mic_switch_id->set_name(Lang::Strings::ESPHOME_ENTITY_SWITCH_NAME_MIC_ENABLE);
  mic_switch_id->set_object_id("mic_switch");
  mic_switch_id->set_disabled_by_default(false);
  mic_switch_id->publish_state(micEnabled());

  volume_number_id = new VolumeNumber();
  esphome::App.register_number(volume_number_id);
  volume_number_id->set_name(Lang::Strings::ESPHOME_ENTITY_NUMBER_NAME_VOLUME);
  volume_number_id->set_object_id("volume_number");
  volume_number_id->set_disabled_by_default(false);
  volume_number_id->traits.set_min_value(0.0f);
  volume_number_id->traits.set_max_value(100.0f);
  volume_number_id->traits.set_step(1.0f);
  volume_number_id->traits.set_mode(esphome::number::NUMBER_MODE_SLIDER);
  volume_number_id->publish_state(outputVolume());

  continuous_dialogue_switch_id = new ContinuousDialogueSwitch();
  esphome::App.register_switch(continuous_dialogue_switch_id);
  continuous_dialogue_switch_id->set_name(Lang::Strings::ESPHOME_ENTITY_SWITCH_NAME_CONTINUOUS_DIALOGUE);
  continuous_dialogue_switch_id->set_object_id("continuous_dialogue_switch");
  continuous_dialogue_switch_id->set_disabled_by_default(false);
  continuous_dialogue_switch_id->publish_state(continuousDialogue());

  sleep_mode_switch_id = new SleepModeSwitch();
  esphome::App.register_switch(sleep_mode_switch_id);
  sleep_mode_switch_id->set_name(Lang::Strings::ESPHOME_ENTITY_SWITCH_NAME_SLEEP_MODE);
  sleep_mode_switch_id->set_object_id("sleep_mode_switch");
  sleep_mode_switch_id->set_disabled_by_default(false);
  sleep_mode_switch_id->publish_state(sleepMode());

  sleep_mode_start_time_id = new SleepModeStartTime();
  esphome::App.register_time(sleep_mode_start_time_id);
  sleep_mode_start_time_id->set_name(Lang::Strings::ESPHOME_ENTITY_TIME_NAME_SLEEP_MODE_START);
  sleep_mode_start_time_id->set_object_id("sleep_mode_start_time");
  sleep_mode_start_time_id->set_disabled_by_default(false);
  sleep_mode_start_time_id->publish_state(_sleepModeTimeInterval.startHour, _sleepModeTimeInterval.startMinute, 0);

  sleep_mode_end_time_id = new SleepModeEndTime();
  esphome::App.register_time(sleep_mode_end_time_id);
  sleep_mode_end_time_id->set_name(Lang::Strings::ESPHOME_ENTITY_TIME_NAME_SLEEP_MODE_END);
  sleep_mode_end_time_id->set_object_id("sleep_mode_end_time");
  sleep_mode_end_time_id->set_disabled_by_default(false);
  sleep_mode_end_time_id->publish_state(_sleepModeTimeInterval.endHour, _sleepModeTimeInterval.endMinute, 0);

  play_voice_text_id = new PlayVoiceText();
  esphome::App.register_text(play_voice_text_id);
  play_voice_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_PLAY_VOICE);
  play_voice_text_id->set_object_id("play_voice_text");
  play_voice_text_id->set_disabled_by_default(false);
  play_voice_text_id->traits.set_min_length(0);
  play_voice_text_id->traits.set_max_length(100);
  play_voice_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  play_voice_text_id->publish_state("");

  execute_command_text_id = new ExecuteCommandText();
  esphome::App.register_text(execute_command_text_id);
  execute_command_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_EXECUTE_COMMAND);
  execute_command_text_id->set_object_id("execute_command_text");
  execute_command_text_id->set_disabled_by_default(false);
  execute_command_text_id->traits.set_min_length(0);
  execute_command_text_id->traits.set_max_length(100);
  execute_command_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  execute_command_text_id->publish_state("");

  ask_and_execute_command_text_id = new AskAndExecuteCommandText();
  esphome::App.register_text(ask_and_execute_command_text_id);
  ask_and_execute_command_text_id->set_name(Lang::Strings::ESPHOME_ENTITY_TEXT_NAME_ASK_AND_EXECUTE_COMMAND);
  ask_and_execute_command_text_id->set_object_id("ask_and_execute_command_text");
  ask_and_execute_command_text_id->set_disabled_by_default(false);
  ask_and_execute_command_text_id->traits.set_min_length(0);
  ask_and_execute_command_text_id->traits.set_max_length(100);
  ask_and_execute_command_text_id->traits.set_mode(esphome::text::TEXT_MODE_TEXT);
  ask_and_execute_command_text_id->publish_state("");
#endif
}

void ESPHomeVoiceDevice::setOutputVolume(uint8_t volume)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)volume;
#else
  _outputVolume = volume;
  Settings settings("esphome", true);
  settings.SetInt("volume", _outputVolume);
  if (volume_number_id != nullptr) {
    volume_number_id->publish_state(volume);
  }

  auto codec = Board::GetInstance().GetAudioCodec();
  updateIsInSleepModeInterval();
  if (codec != nullptr) {
    codec->SetOutputVolume(_sleepMode && _isInSleepModeInterval && volume > 20 ? 20 : volume);
  }
  BLEManager::GetInstance().notifyVolume(volume);
  ESP_LOGI(TAG, "Set output volume to %d", volume);
#endif
}

void ESPHomeVoiceDevice::setMicEnable(bool enabled)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)enabled;
#else
  _micEnabled = enabled;
  Settings settings("esphome", true);
  settings.SetBool("micEnabled", _micEnabled);
  if (mic_switch_id != nullptr) {
    mic_switch_id->publish_state(_micEnabled);
  }
  BLEManager::GetInstance().notifyMicSwitchState(_micEnabled);
  ESP_LOGI(TAG, "Set mic enabled to %d", _micEnabled);
#endif
}

void ESPHomeVoiceDevice::setContinuousDialogue(bool enabled)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)enabled;
#else
  _continuousDialogue = enabled;
  Settings settings("esphome", true);
  settings.SetBool("cDialogue", _continuousDialogue);
  if (continuous_dialogue_switch_id != nullptr) {
    continuous_dialogue_switch_id->publish_state(_continuousDialogue);
  }
  BLEManager::GetInstance().notifyContinuousDialogue(_continuousDialogue);
#endif
}

void ESPHomeVoiceDevice::setVoiceResponseSound(bool enabled)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)enabled;
#else
  _voiceResponseSound = enabled;
  Settings settings("esphome", true);
  settings.SetBool("vrSound", _voiceResponseSound);
  BLEManager::GetInstance().notifyVoiceResponseSound(_voiceResponseSound);
#endif
}

void ESPHomeVoiceDevice::setPlayVoiceText(const std::string& value)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)value;
#else
  Board::GetInstance().GetVoiceController()->playVoiceText(value);
  if (play_voice_text_id != nullptr) {
    play_voice_text_id->publish_state("");
  }
#endif
}

void ESPHomeVoiceDevice::setExecuteCommandText(const std::string& value)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)value;
#else
  Board::GetInstance().GetVoiceController()->executeCommandText(value);
  if (execute_command_text_id != nullptr) {
    execute_command_text_id->publish_state("");
  }
#endif
}

void ESPHomeVoiceDevice::setAskAndExecuteCommandText(const std::string& value)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)value;
#else
  Board::GetInstance().GetVoiceController()->askAndExecuteCommandText(value);
  if (ask_and_execute_command_text_id != nullptr) {
    ask_and_execute_command_text_id->publish_state("");
  }
#endif
}

void ESPHomeVoiceDevice::setSleepMode(bool enabled)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)enabled;
#else
  _sleepMode = enabled;
  Settings settings("esphome", true);
  settings.SetBool("sleepMode", _sleepMode);
  if (sleep_mode_switch_id != nullptr) {
    sleep_mode_switch_id->publish_state(_sleepMode);
  }
  BLEManager::GetInstance().notifySleepMode(_sleepMode);
  updateOutputVolume();
#endif
}

void ESPHomeVoiceDevice::setSleepModeTimeInterval(uint32_t timeInterval)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)timeInterval;
#else
  _sleepModeTimeInterval.setSleepModeTimeInterval(timeInterval);
  Settings settings("esphome", true);
  settings.setUint32("sleepModeTI", _sleepModeTimeInterval.getSleepModeTimeInterval());
  if (sleep_mode_start_time_id != nullptr) {
    sleep_mode_start_time_id->publish_state(_sleepModeTimeInterval.startHour, _sleepModeTimeInterval.startMinute, 0);
  }
  if (sleep_mode_end_time_id != nullptr) {
    sleep_mode_end_time_id->publish_state(_sleepModeTimeInterval.endHour, _sleepModeTimeInterval.endMinute, 0);
  }
  BLEManager::GetInstance().notifySleepModeTimeInterval(_sleepModeTimeInterval.getSleepModeTimeInterval());
  updateOutputVolume();
#endif
}

void ESPHomeVoiceDevice::setSleepModeStartTime(uint8_t hour, uint8_t minute)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)hour;
  (void)minute;
#else
  _sleepModeTimeInterval.setSleepModeTimeInterval(hour, minute, _sleepModeTimeInterval.endHour, _sleepModeTimeInterval.endMinute);
  setSleepModeTimeInterval(_sleepModeTimeInterval.getSleepModeTimeInterval());
#endif
}

void ESPHomeVoiceDevice::setSleepModeEndTime(uint8_t hour, uint8_t minute)
{
#if !CONFIG_USE_VOICE_DIALOGUE
  (void)hour;
  (void)minute;
#else
  _sleepModeTimeInterval.setSleepModeTimeInterval(_sleepModeTimeInterval.startHour, _sleepModeTimeInterval.startMinute, hour, minute);
  setSleepModeTimeInterval(_sleepModeTimeInterval.getSleepModeTimeInterval());
#endif
}

void ESPHomeVoiceDevice::updateIsInSleepModeInterval()
{
#if CONFIG_USE_VOICE_DIALOGUE
  if (!_sleepMode) {
    _isInSleepModeInterval = false;
    return;
  }

  time_t now = time(nullptr);
  struct tm* tm = localtime(&now);
  if (tm == nullptr || tm->tm_year < 2025 - 1900) {
    _isInSleepModeInterval = false;
    return;
  }

  uint32_t startTime = _sleepModeTimeInterval.startTime();
  uint32_t endTime = _sleepModeTimeInterval.endTime();
  uint32_t current = tm->tm_hour * 60 + tm->tm_min;
  if (endTime >= 24 * 60 && current < startTime) {
    current += 24 * 60;
  }
  _isInSleepModeInterval = current >= startTime && current <= endTime;
#endif
}

void ESPHomeVoiceDevice::updateOutputVolume()
{
#if CONFIG_USE_VOICE_DIALOGUE
  setOutputVolume(outputVolume());
#endif
}

bool ESPHomeVoiceDevice::micEnabled() const
{
#if CONFIG_USE_VOICE_DIALOGUE
  return _micEnabled;
#else
  return true;
#endif
}

uint8_t ESPHomeVoiceDevice::outputVolume() const
{
#if CONFIG_USE_VOICE_DIALOGUE
  return _outputVolume;
#else
  return 70;
#endif
}

bool ESPHomeVoiceDevice::continuousDialogue() const
{
#if CONFIG_USE_VOICE_DIALOGUE
  return _continuousDialogue;
#else
  return true;
#endif
}

bool ESPHomeVoiceDevice::voiceResponseSound() const
{
#if CONFIG_USE_VOICE_DIALOGUE
  return _voiceResponseSound;
#else
  return false;
#endif
}

bool ESPHomeVoiceDevice::sleepMode() const
{
#if CONFIG_USE_VOICE_DIALOGUE
  return _sleepMode;
#else
  return false;
#endif
}

uint32_t ESPHomeVoiceDevice::sleepModeTimeInterval()
{
#if CONFIG_USE_VOICE_DIALOGUE
  return _sleepModeTimeInterval.getSleepModeTimeInterval();
#else
  return 0;
#endif
}
