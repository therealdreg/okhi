/*
Dreg's note:

I've modified this project https://github.com/ataradov/usb-sniffer-lite 
for my own needs for the OKHI keylogger. I've added a lot of code that's 
pretty much junk and it kind of works... I'll improve it in the future.

Copyright (c) [2024] by David Reguera Garcia aka Dreg 
https://github.com/therealdreg/okhi
https://www.rootkit.es
X @therealdreg
dreg@rootkit.es

Thx Alex Taradov <alex@taradov.com> for the original work!
*/

#ifndef _CONF_H_
#define _CONF_H_

#define F_REF      12000000
#define F_CPU      120000000
#define F_PER      120000000
#define F_RTC      (F_REF / 256)
#define F_TICK     1000000

#define BAUD_RATE 230400

#endif /* _CONF_H_ */