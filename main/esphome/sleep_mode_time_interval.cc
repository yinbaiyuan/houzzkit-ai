#include "sleep_mode_time_interval.h"

void SleepModeTimeInterval::setSleepModeTimeInterval(uint32_t time)
{
    startHour = (time >> 24) & 0xFF;
    startMinute = (time >> 16) & 0xFF;
    endHour = (time >> 8) & 0xFF;
    endMinute = time & 0xFF;
}

void SleepModeTimeInterval::setSleepModeTimeInterval(uint8_t startHour, uint8_t startMinute, uint8_t endHour, uint8_t endMinute)
{
    this->startHour = startHour;
    this->startMinute = startMinute;
    this->endHour = endHour;
    this->endMinute = endMinute;
}

uint32_t SleepModeTimeInterval::getSleepModeTimeInterval()
{
    return (startHour << 24) | (startMinute << 16) | (endHour << 8) | endMinute;
}
