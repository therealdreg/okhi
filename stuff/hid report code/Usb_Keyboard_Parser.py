import subprocess,sys,os
import shlex,string
usb_codes = {
    "0x04":['a','A'],"0x05":['b','B'], "0x06":['c','C'], "0x07":['d','D'], "0x08":['e','E'], "0x09":['f','F'],"0x0A":['g','G'],"0x0B":['h','H'], "0x0C":['i','I'], "0x0D":['j','J'], "0x0E":['k','K'], "0x0F":['l','L'],"0x10":['m','M'], "0x11":['n','N'], "0x12":['o','O'], "0x13":['p','P'], "0x14":['q','Q'], "0x15":['r','R'],"0x16":['s','S'], "0x17":['t','T'], "0x18":['u','U'], "0x19":['v','V'], "0x1A":['w','W'], "0x1B":['x','X'],"0x1C":['y','Y'], "0x1D":['z','Z'], "0x1E":['1','!'], "0x1F":['2','@'], "0x20":['3','#'], "0x21":['4','$'],"0x22":['5','%'], "0x23":['6','^'], "0x24":['7','&'], "0x25":['8','*'], "0x26":['9','('], "0x27":['0',')'],"0x28":['\n','\n'], "0x29":['[ESC]','[ESC]'], "0x2A":['[BACKSPACE]','[BACKSPACE]'], "0x2B":['\t','\t'],"0x2C":[' ',' '], "0x2D":['-','_'], "0x2E":['=','+'], "0x2F":['[','{'], "0x30":[']','}'], "0x31":['\',"|'],"0x32":['#','~'], "0x33":";:", "0x34":"'\"", "0x36":",<",  "0x37":".>", "0x38":"/?","0x39":['[CAPSLOCK]','[CAPSLOCK]'], "0x3A":['F1'], "0x3B":['F2'], "0x3C":['F3'], "0x3D":['F4'], "0x3E":['F5'], "0x3F":['F6'], "0x41":['F7'], "0x42":['F8'], "0x43":['F9'], "0x44":['F10'], "0x45":['F11'],"0x46":['F12'], "0x4F":[u'→',u'→'], "0x50":[u'←',u'←'], "0x51":[u'↓',u'↓'], "0x52":[u'↑',u'↑']
   }
data = "usb.capdata"
filepath = sys.argv[1]

def keystroke_decoder(filepath,data):
    out = subprocess.run(shlex.split("tshark -r  %s -Y \"%s\" -T fields -e %s"%(filepath,data,data)),capture_output=True)
    output = out.stdout.split() # Last 8 bytes of URB_INTERPRUT_IN
    message = []
    modifier =0
    count =0
    for i in range(len(output)):
        buffer = str(output[i])[2:-1]
        if (buffer)[:2] == "02" or (buffer)[:2] == "20":
            for j in range(1):
                count +=1 
                m ="0x" + buffer[4:6].upper()
                if m in usb_codes and m == "0x2A": message.pop(len(message)-1)
                elif m in usb_codes: message.append(usb_codes.get(m)[1])
                else: break
        else:
            if buffer[:2] == "01": 
                modifier +=1
                continue   
            for j in range(1):
                count +=1 
                m  = "0x" + buffer[4:6].upper()
                if m in usb_codes and m == "0x2A": message.pop(len(message)-1)
                elif m in usb_codes : message.append(usb_codes.get(m)[0])
                else: break

    if modifier != 0:
        print(f'[-] Found Modifier in {modifier} packets [-]')
    return message

if len(sys.argv) != 2 or os.path.exists(filepath) != 1:
    print("\nUsage : ")
    print("\npython Usb_Keyboard_Parser.py <filepath>")
    print("Created by \t\t\t Sabhya <sabhrajmeh05@gmail.com\n")
    print("Must Install tshark & subprocess first to use it\n")
    print("To install run \"sudo apt install tshark\"")
    print("To install run \"pip install subprocess.run\"")
    exit(1)

function_call = keystroke_decoder(filepath,data)
hid_data =''

for _ in range(len(function_call)): hid_data += function_call[_]

if(hid_data == ''):
    function_call = keystroke_decoder(filepath, "usbhid.data")
    print("\n[+] Using filter \"usbhid.data\" Retrived HID Data is : \n")
    for _ in range(len(function_call)): print(function_call[_],end='')
    print("\n")
else:
    print("\n[+] Using filter \"usb.capdata\" Retrived HID Data is : \n")
    print(hid_data)

