/*
MIT License - okhi - Open Keylogger Hardware Implant
---------------------------------------------------------------------------
Copyright (c) [2024] by David Reguera Garcia aka Dreg 
https://github.com/therealdreg/okhi
https://www.rootkit.es
X @therealdreg
dreg@rootkit.es
---------------------------------------------------------------------------
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
---------------------------------------------------------------------------
WARNING: BULLSHIT CODE X-)
---------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define TARGET 0xCCCCCC

int main(int argc, char *argv[]) {
    FILE *inputFile, *outputFile, *crcFile;
    long fileSize;
    uint8_t *buffer;
    uint32_t crcValue;
    char line[256] = {0};

    puts("hexup v1.0 by Dreg");

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <CRC file> <input file> <output file>\n", argv[0]);
        return 1;
    }

    crcFile = fopen(argv[1], "r");
    if (crcFile == NULL) {
        perror("Error opening CRC output file");
        return 1;
    }

    while (fgets(line, sizeof(line), crcFile)) {
        if (sscanf(line, "CRC value is 0x%x", &crcValue) == 1) {
            break;
        }
    }
    fclose(crcFile);

    printf("CRC value read: 0x%08X\n", crcValue);

    inputFile = fopen(argv[2], "rb");
    if (inputFile == NULL) {
        perror("Error opening input file");
        return 2;
    }

    fseek(inputFile, 0, SEEK_END);
    fileSize = ftell(inputFile);
    rewind(inputFile);

    buffer = (uint8_t*)calloc(1, fileSize);
    if (buffer == NULL) {
        perror("Memory error");
        fclose(inputFile);
        return 3;
    }

    fread(buffer, 1, fileSize, inputFile);
    fclose(inputFile);

    for (long i = 0; i < fileSize - 3; i++) {
        if (buffer[i] == 0xCC && buffer[i+1] == 0xCC && buffer[i+2] == 0xCC) {
            buffer[i+3] = (crcValue >> 24) & 0xFF;
            buffer[i+2] = (crcValue >> 16) & 0xFF;
            buffer[i+1] = (crcValue >> 8) & 0xFF;
            buffer[i+0] = crcValue & 0xFF;
            break;
        }
    }

    // Check if output file already exists
    outputFile = fopen(argv[3], "rb");
    if (outputFile != NULL) {
        fclose(outputFile);
        fprintf(stderr, "Error: Output file '%s' already exists.\n", argv[3]);
        free(buffer);
        return 4;
    }

    // Create the file since it does not exist
    outputFile = fopen(argv[3], "wb");
    if (outputFile == NULL) {
        perror("Error creating output file");
        free(buffer);
        return 5;
    }

    fwrite(buffer, 1, fileSize, outputFile);
    fclose(outputFile);

    free(buffer);
    printf("First occurrence of 0xCCCCCC replaced with 0x%08X\n", crcValue);

    return 0;
}
