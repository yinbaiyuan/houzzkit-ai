#pragma once

#include <stdio.h>

class SleepModeTimeInterval
{
    public:
        uint8_t startHour = 0;
        uint8_t startMinute = 0;
        uint8_t endHour = 0;
        uint8_t endMinute = 0;

    public:

        SleepModeTimeInterval(){};

        ~SleepModeTimeInterval(){};

        void setSleepModeTimeInterval(uint32_t time);

        void setSleepModeTimeInterval(uint8_t startHour, uint8_t startMinute, uint8_t endHour, uint8_t endMinute);

        uint32_t getSleepModeTimeInterval();
};
