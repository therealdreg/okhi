#!/usr/bin/env python3

# by Dreg
def file_to_c_string(filepath):
    try:
        with open(filepath, 'rb') as file:
            file_contents = file.read()
        hex_string = ''.join(f'\\x{byte:02x}' for byte in file_contents)
        line_length = 40
        lines = []
        for i in range(0, len(hex_string), line_length):
            lines.append(hex_string[i:i+line_length])
        return '" \n"'.join(lines)
    except Exception as e:
        return f"Error reading: {e}"
        
c_string = file_to_c_string('index.html')
c_string = 'PGM_P web = \n{\n"' + c_string + '" \n};' 
print(c_string)
# pip install pyperclip
import pyperclip
pyperclip.copy(c_string)
print("\n\nCopied to clipboard\n")
