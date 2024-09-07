#!/usr/bin/env python2

"""
Try and translate a pcap of USB HID to ascii text.
The data is in "USB Leftover section" - either use wireshark or tshark directly to extact the data.

tshark -r ./my.pcapng -T fields -e usb.capdata -Y usb.capdata 2>/dev/null

The output should look like:
00:00:25:00:00:00:00:00
00:00:00:00:00:00:00:00
00:00:21:00:00:00:00:00
00:00:00:00:00:00:00:00
00:00:26:00:00:00:00:00
00:00:00:00:00:00:00:00
00:00:22:00:00:00:00:00
00:00:00:00:00:00:00:00

The first is used for modifiers, we unly use 0x20 here. (shift)
The third is the keycode for the key.
The forth is errorbit - here used for long keypresses

If all is 00, this is a key release

"""
import string
import sys


def usb_to_ascii(num, mod=0):
    # map keys
    lower = '????' + string.ascii_lowercase + "1234567890" + "\n??\t -=[]\\?;'`,./?"
    upper = '????' + string.ascii_uppercase + "!@#$%^&*()" + "\n??\t _|{}|?:\"~<>??"

    # Default to lower
    chars = lower
    
    if mod == 32:
        chars = upper
    if num == 42:
        # Hack in backspace
        return '\x08'
    if 0<= num < len(chars):
        # print num, chars[num]
        return chars[num]

text = ""
for line in sys.stdin.readlines():
    mod, spam, val, err = line.split(":")[:4] # Only use for 4 first
    # Convert from base 16
    val = int(val, 16)
    mod = int(mod, 16)
    err = int(err, 16)
    if err is 0: # Only append if error bit isn't set (This will exclude long keypresses)
        if val: # Only continue if this is not a keyrelease
            char = usb_to_ascii(val, mod=mod)
            if char is not None:
                text += char

print text
