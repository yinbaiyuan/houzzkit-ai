#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/display.h"
#include "display/emote_display.h"
#include "display/lcd_display.h"
#include "esp_lcd_ili9341.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#define TAG "EspBox3Board"

// Init ili9341 by custom cmd
static const ili9341_lcd_init_cmd_t vendor_specific_init[] = {
    {0xC8, (uint8_t []){0xFF, 0x93, 0x42}, 3, 0},
    {0xC0, (uint8_t []){0x0E, 0x0E}, 2, 0},
    {0xC5, (uint8_t []){0xD0}, 1, 0},
    {0xC1, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0x00, 0x03, 0x08, 0x06, 0x13, 0x09, 0x39, 0x39, 0x48, 0x02, 0x0a, 0x08, 0x17, 0x17, 0x0F}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x28, 0x29, 0x01, 0x0d, 0x03, 0x3f, 0x33, 0x52, 0x04, 0x0f, 0x0e, 0x37, 0x38, 0x0F}, 15, 0},

    {0xB1, (uint8_t []){00, 0x1B}, 2, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xB7, (uint8_t []){0x06}, 1, 0},

    {0x11, (uint8_t []){0}, 0x80, 0},
    {0x29, (uint8_t []){0}, 0x80, 0},

    {0, (uint8_t []){0}, 0xff, 0},
};


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
            Application::GetInstance().setMicEnabled(level == 1);
            Board::GetInstance().GetLed()->OnStateChanged();
        }
    }
}

class HouzzkitCubeBoard : public WifiBoard {
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

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_6;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_7;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif

        play_button_.OnClick([this]() {
            // power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            app.ToggleChatState();
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
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
            
        });

        volume_up_button_.OnLongPress([this]() {
            // power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
        });

        volume_down_button_.OnClick([this]() {
            // power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume/10));
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
        });

        volume_down_button_.OnLongPress([this]() {
            // power_save_timer_->WakeUp();
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
        });
    }

    void InitializeIli9341Display() {
        // esp_lcd_panel_io_handle_t panel_io = nullptr;
        // esp_lcd_panel_handle_t panel = nullptr;

        // // 液晶屏控制IO初始化
        // ESP_LOGD(TAG, "Install panel IO");
        // esp_lcd_panel_io_spi_config_t io_config = {};
        // io_config.cs_gpio_num = GPIO_NUM_5;
        // io_config.dc_gpio_num = GPIO_NUM_4;
        // io_config.spi_mode = 0;
        // io_config.pclk_hz = 40 * 1000 * 1000;
        // io_config.trans_queue_depth = 10;
        // io_config.lcd_cmd_bits = 8;
        // io_config.lcd_param_bits = 8;
        // ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // // 初始化液晶屏驱动芯片
        // ESP_LOGD(TAG, "Install LCD driver");
        // const ili9341_vendor_config_t vendor_config = {
        //     .init_cmds = &vendor_specific_init[0],
        //     .init_cmds_size = sizeof(vendor_specific_init) / sizeof(ili9341_lcd_init_cmd_t),
        // };

        // esp_lcd_panel_dev_config_t panel_config = {};
        // panel_config.reset_gpio_num = GPIO_NUM_48;
        // panel_config.flags.reset_active_high = 1,
        // panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        // panel_config.bits_per_pixel = 16;
        // panel_config.vendor_config = (void *)&vendor_config;
        // ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
        // esp_lcd_panel_reset(panel);
        // esp_lcd_panel_init(panel);
        // esp_lcd_panel_invert_color(panel, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        // esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        // esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        // esp_lcd_panel_disp_on_off(panel, true);

#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#else
        // display_ = new SpiLcdDisplay(panel_io, panel,
        //     DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = new NoDisplay();
#endif
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
    HouzzkitCubeBoard() : boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(BUTTON_VOLUME_UP_GPIO),
        volume_down_button_(BUTTON_VOLUME_DOWN_GPIO),
        play_button_(BUTTON_PLAY_GPIO) {
        InitializeI2c();
        // InitializeSpi();
        InitializeIli9341Display();
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
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }


    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(HouzzkitCubeBoard);
