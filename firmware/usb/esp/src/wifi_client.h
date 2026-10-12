/*
MIT License - okhi - Open Keylogger Hardware Implant
Copyright (c) [2024] by David Reguera Garcia aka Dreg
https://github.com/therealdreg/okhi
*/


#ifndef __WIFI_CLIENT_H__
#define __WIFI_CLIENT_H__

#if defined(__has_include)
#if __has_include("wifi_secret.h")
#include "wifi_secret.h"
#pragma message("wifi_secret.h is compiled in: this binary CONTAINS YOUR WIFI PASSWORD in clear text, and firmware/*/esp/build/okhi.bin and okhi.elf are TRACKED IN GIT. Do not commit or release this build.")
#endif
#endif

#endif
