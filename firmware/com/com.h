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

#ifndef __COM_COM__
#define __COM_COM__

#define OKHI_VARIANT_PS2 "ps2"
#define OKHI_VARIANT_USB "usb"

#define OKHI_PACKAGE_TAG_PS2 "PS2"
#define OKHI_PACKAGE_TAG_USB "USB"

#define SPI_FRAME_SIZE 256
#define SPI_FRAME_VERSION 3

#define SPI_FRAME_TYPE_STATUS 1
#define SPI_FRAME_TYPE_DATA 2

#define SPI_CTRL_TYPE_POLL 1
#define SPI_CTRL_TYPE_REQUEST_BLOCK 2

#define SPI_FRAME_MAGIC_BYTES 'O', 'K', 'H', 'I'
#define SPI_CTRL_MAGIC_BYTES 'O', 'K', 'H', 'C'

#define SPI_FRAME_LEAD 16
#define SPI_PAYLOAD_FROM_MAGIC 96
#define SPI_BLOCK_PAYLOAD 128
#define SPI_PAYLOAD_OFFSET (SPI_FRAME_LEAD + SPI_PAYLOAD_FROM_MAGIC)
#define SPI_NO_BLOCK 0xFFFFFFFFu

/*
 * SPI_MAGIC_SEARCH_MAX CANNOT BE RAISED. ota_fetch_block() copies
 * SPI_PAYLOAD_FROM_MAGIC + up to SPI_BLOCK_PAYLOAD bytes measured from the
 * magic, so the worst case is 32 + 96 + 128 = 256, exactly SPI_FRAME_SIZE.
 * A larger window lets a block read run past the end of the receive buffer.
 */
#define SPI_MAGIC_SEARCH_MAX 32

#define SPI_IDENTITY_OFFSET 16
#define SPI_IDENTITY_MAX 64

#define SPI_FLAG_BENCH_ARMED 0x01
#define SPI_FLAG_RP_IMAGE_READY 0x02
#define SPI_FLAG_RP_COMMIT 0x04

#define RP_UART_BAUD 74880

#endif // __COM_COM__
