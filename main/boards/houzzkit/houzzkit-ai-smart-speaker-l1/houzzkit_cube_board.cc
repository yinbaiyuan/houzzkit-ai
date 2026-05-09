#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "esphome_device.h"
#include "esphome_voice_device.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>

#define TAG "HouzzkitSmartSpeaker"

// GPIO1中断相关定义
#define GPIO1_PIN GPIO_NUM_1
static QueueHandle_t gpio1_evt_queue = NULL;

// GPIO1中断服务程序
static void IRAM_ATTR gpio1_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio1_evt_queue, &gpio_num, NULL);
}

// GPIO1中断事件处理任务
static void gpio1_task(void* arg) {
    uint32_t io_num;
    while(1) {
        if(xQueueReceive(gpio1_evt_queue, &io_num, portMAX_DELAY)) {
            // 在这里添加GPIO1状态变化时需要执行的操作
            ESP_LOGI(TAG, "GPIO1 pin state changed");
            // 例如：触发某个事件、控制设备等
            // 注意：在中断处理任务中避免执行耗时操作
            //获取GPIO1的当前状态
            int level = gpio_get_level((gpio_num_t)io_num);
            ESP_LOGI(TAG, "GPIO1 level: %d", level);
            // 控制麦克风使能
#if CONFIG_USE_VOICE_DIALOGUE
            ESPHomeVoiceDevice::GetInstance().setMicEnable(level == 1);
#else
            (void)level;
#endif
            Board::GetInstance().GetLed()->OnStateChanged();
        }
    }
}

class HouzzkitSmartSpeakerBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Button play_button_;

    Display* display_;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
#if CONFIG_USE_VOICE_DIALOGUE
            Board::GetInstance().GetVoiceController()->ToggleChatState();
#endif
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateRunning && Board::GetInstance().GetVoiceController()->IsIdle()) {
                auto voice = Board::GetInstance().GetVoiceController();
                voice->SetAecMode(voice->GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif

        play_button_.OnClick([this]() {
            // power_save_timer_->WakeUp();
            // auto& app = Application::GetInstance();
            // Board::GetInstance().GetVoiceController()->ToggleChatState();

            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
#if CONFIG_USE_VOICE_DIALOGUE
            Board::GetInstance().GetVoiceController()->ToggleChatState();
#endif
        });

        play_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            // Board::GetInstance().GetVoiceController()->ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            // power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
#if CONFIG_USE_VOICE_DIALOGUE
            Board::GetInstance().GetVoiceController()->PlaySound(Lang::Sounds::OGG_POPUP);
#endif

        });

        volume_up_button_.OnLongPress([this]() {
            // power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
#if CONFIG_USE_VOICE_DIALOGUE
            Board::GetInstance().GetVoiceController()->PlaySound(Lang::Sounds::OGG_POPUP);
#endif
        });

        volume_down_button_.OnClick([this]() {
            // power_save_timer_->WakeUp();
//             auto codec = GetAudioCodec();
//             auto volume = codec->output_volume() - 10;
//             if (volume < 0) {
//                 volume = 0;
//             }
//             codec->SetOutputVolume(volume);
//             GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
// #if CONFIG_USE_VOICE_DIALOGUE
//             Board::GetInstance().GetVoiceController()->PlaySound(Lang::Sounds::OGG_POPUP);
// #endif
        });

        volume_down_button_.OnLongPress([this]() {
            // power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
#if CONFIG_USE_VOICE_DIALOGUE
            Board::GetInstance().GetVoiceController()->PlaySound(Lang::Sounds::OGG_POPUP);
#endif
        });
    }

    void InitializeDisplay() {
        display_ = new NoDisplay();
    }

    void InitializeGPIO1Interrupt() {
        // 创建事件队列
        gpio1_evt_queue = xQueueCreate(10, sizeof(uint32_t));

        // 配置GPIO1引脚
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_ANYEDGE;  // 双边沿中断
        io_conf.mode = GPIO_MODE_INPUT;         // 输入模式
        io_conf.pin_bit_mask = (1ULL << GPIO1_PIN);  // GPIO1
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;   // 启用上拉
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;  // 禁用下拉
        gpio_config(&io_conf);

        // 安装GPIO ISR服务
        gpio_install_isr_service(0);

        // 添加GPIO1中断处理函数
        gpio_isr_handler_add(GPIO1_PIN, gpio1_isr_handler, (void*)GPIO1_PIN);

        // 创建GPIO1中断处理任务
        xTaskCreate(&gpio1_task, "gpio1_task", 2048, NULL, 10, NULL);
    }

public:
    HouzzkitSmartSpeakerBoard() : boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(BUTTON_VOLUME_UP_GPIO),
        volume_down_button_(BUTTON_VOLUME_DOWN_GPIO),
        play_button_(BUTTON_PLAY_GPIO) {
        InitializeI2c();
        InitializeDisplay();
        InitializeButtons();
        InitializeGPIO1Interrupt();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE,
            AUDIO_CODEC_PA_REVERTED);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual void RegisterESPHomeEntities(ESPHomeDevice& device) override {
        ESPHomeVoiceDevice::GetInstance().RegisterEntities(device);
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(HouzzkitSmartSpeakerBoard);
