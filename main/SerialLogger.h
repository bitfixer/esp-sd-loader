#ifndef __serial_logger_h__
#define __serial_logger_h__

#include <stdio.h>
#include <esp_log.h>
#include "Logger.h"

namespace bitfixer
{

class SerialLogger : public Logger {
public:
    SerialLogger()
    {

    }

    void init()
    {
    }

    void initWithSerial()
    {
    }

    void log(const char* str)
    {
        ::printf(str);
        //ESP_LOGI("log", str);
    }

private:
};

}

#endif