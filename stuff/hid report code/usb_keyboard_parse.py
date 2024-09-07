#!/usr/bin/env python3

import subprocess
import sys

# USB keyboard HID key codes
usb_codes = {
   0x04:['a','A'], 0x05:['b','B'], 0x06:['c','C'], 0x07:['d','D'], 0x08:['e','E'], 0x09:['f','F'],
   0x0A:['g','G'], 0x0B:['h','H'], 0x0C:['i','I'], 0x0D:['j','J'], 0x0E:['k','K'], 0x0F:['l','L'],
   0x10:['m','M'], 0x11:['n','N'], 0x12:['o','O'], 0x13:['p','P'], 0x14:['q','Q'], 0x15:['r','R'],
   0x16:['s','S'], 0x17:['t','T'], 0x18:['u','U'], 0x19:['v','V'], 0x1A:['w','W'], 0x1B:['x','X'],
   0x1C:['y','Y'], 0x1D:['z','Z'], 0x1E:['1','!'], 0x1F:['2','@'], 0x20:['3','#'], 0x21:['4','$'],
   0x22:['5','%'], 0x23:['6','^'], 0x24:['7','&'], 0x25:['8','*'], 0x26:['9','('], 0x27:['0',')'],
   0x28:['\n','\n'], 0x29:['[ESC]','[ESC]'], 0x2A:['[BACKSPACE]','[BACKSPACE]'], 0x2B:['\t','\t'],
   0x2C:[' ',' '], 0x2D:['-','_'], 0x2E:['=','+'], 0x2F:['[','{'], 0x30:[']','}'], 0x31:['\',"|'],
   0x32:['#','~'], 0x33:";:", 0x34:"'\"", 0x36:",<",  0x37:".>", 0x38:"/?",
   0x39:['[CAPSLOCK]','[CAPSLOCK]'], 0x3A:['F1'], 0x3B:['F2'], 0x3C:['F3'], 0x3D:['F4'], 0x3E:['F5'], 
   0x3F:['F6'], 0x41:['F7'], 0x42:['F8'], 0x43:['F9'], 0x44:['F10'], 0x45:['F11'],
   0x46:['F12'], 0x4f:[u'→',u'→'], 0x50:[u'←',u'←'], 0x51:[u'↓',u'↓'], 0x52:[u'↑',u'↑']
   }

# Read pcap/pcapng file path containing USB data
def read_pcap():
    pcap_file = sys.argv[1]
    usb_capdata = subprocess.getoutput("tshark -r {} -T fields -e usb.capdata -Y 'usb.capdata && usb.data_len == 8'".format(pcap_file))
    usb_capdata = usb_capdata.splitlines()
    return usb_capdata

# Get keyboard data from usb.capdata
def check_keyboard_data(usb_capdata):
    if(len(usb_capdata)!=0):
        print("[+] Found USB Keyboard Hid data\n")
        return True
    else:
        print("[+] Not Fount USB Keyboard Hid data\n")
        return False

# Retrieving Keystrokes data
def retrieve_keystrokes(usb_keyboard_data):
    l = [""]
    a=[]
    pos=0
    
    for i in range(0,len(usb_keyboard_data)):
        d=str(usb_keyboard_data[i])
        d=d[0:23]
        if((d[0:2]=="00" or d[0:2]=="02") and d[6:8] != "00"):
            a.append(d)

    for x in a:
        code = int(x[6:8],16)
        if code == 0:
            continue
        # newline or down arrow - move down
        if code == 0x51 or code == 0x28:
            pos += 1
            l.append("")
            continue
        # up arrow - move up
        if code == 0x52:
            pos -= 1
            continue
        # select the character based on the Shift key
        if int(x[0:2],16) == 2:
            l[pos] += usb_codes[code][1]
        else:
            l[pos] += usb_codes[code][0]
    l="\n".join(l)
    return(l)

if __name__ == '__main__':
    if(len(sys.argv)<2):
        print('''[+] Error reading file\nUsage: python usb_parse.py <Path to the pcap/pcapng file>''')
        exit(-1)
    else:
        capdata = read_pcap()
        if(check_keyboard_data(capdata)):
            data = retrieve_keystrokes(capdata)
        print("Retrieved Keyboard Hid data is:") # print the keystrokes
        # print(data)
        sys.stdout.write(data + "\n")