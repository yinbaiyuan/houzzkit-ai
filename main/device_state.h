#ifndef _DEVICE_STATE_H_
#define _DEVICE_STATE_H_

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateRunning,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
};

enum VoiceInteractionState {
    kVoiceInteractionStateIdle,
    kVoiceInteractionStateConnecting,
    kVoiceInteractionStateListening,
    kVoiceInteractionStateSpeaking,
    kVoiceInteractionStateAudioTesting
};

#endif // _DEVICE_STATE_H_
