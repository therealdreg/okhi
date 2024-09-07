

/*

There might still be some code/deadcode from this project 
(I've changed a lot of things): https://github.com/lurk101/ps2kbd-lib

int __attribute__((noinline)) kbd_ready(void) 
{
    if (ascii) // We might already have a character
        return ascii;
    if (pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
        return 0; // no new codes in the fifo
    // pull a scan code from the PIO SM fifo
    uint8_t code = *((io_rw_8*)&pio0->rxf[kbd_sm] + 3);
    switch (code)
    {
        case 0xF0:               // key-release code 0xF0 detected
            release = 1;         // set release
            break;               // go back to start
        case 0x12:               // Left-side SHIFT key detected
        case 0x59:               // Right-side SHIFT key detected
            if (release)
            {       // L or R Shift detected, test release
                shift = 0;       // Key released preceded  this Shift, so clear shift
                release = 0;     // Clear key-release flag
            } 
            else
                shift = 1; // No previous Shift detected before now, so set Shift_Key flag now
            break;
        default:
            // no case applies
            if (!release)                              // If no key-release detected yet
                ascii = (shift ? upper : lower)[code]; // Get ASCII value by case
            release = 0;
            break;
    }
    return ascii;
}

char kbd_getc(void) 
{
    char c;
    while (!(c = kbd_ready()))
    {
        tight_loop_contents();
    }
    ascii = 0;
    return c;
}

typedef enum 
{
	GSYR_OK = 0,
	GSYR_ERROR,
	GSYR_NEED_MORE_BYTES = 1,
	
} get_sym_ret_t;

get_sym_ret_t get_sym(unsigned char* buff, size_t size, unsigned char** next, bool* press, char** sym)
{
	if (size == 0 || buff == NULL || next == NULL || press == NULL || sym == NULL)
	{
		return GSYR_ERROR;
	}

	*press = true;
	
	// 0xE1 Special only press case
	if (*buff == 0xE1)
	{
		if (size < 8)
		{
			return GSYR_NEED_MORE_BYTES;
		}
		
		if (   buff[1] == 0x14 && buff[2] == 0x77
			&& buff[3] == 0xE1 && buff[4] == 0xF0 
			&& buff[5] == 0x14 && buff[6] == 0xF0
			&& buff[7] == 0x77)
		{
			 *next = buff + 8; *sym = "<PAUSE>"; return GSYR_OK;
		}
			
		return GSYR_ERROR;
	}
	
	// Handle release case & 0xE0 case (press & releases)
	if (*buff == 0xF0 || *buff == 0xE0)
	{
		if (size == 1)
		{
			return GSYR_NEED_MORE_BYTES;
		}
		
		if (*buff == 0xF0)
		{
			*press = false;
			buff++;
		}
		else if (*buff == 0xE0)
		{
			buff++;
			if (*buff == 0xF0)
			{
				*press = false;
				if (size == 2)
				{
					return GSYR_NEED_MORE_BYTES;
				}
				buff++;

                if (*buff == 0x7C)
                {
                    if (size < 6)
                    {
                        return GSYR_NEED_MORE_BYTES;
                    }
                    if (buff[1] == 0xE0 && buff[2] == 0xF0 && buff[3] == 0x12)
                    {
                        *next = buff + 4; *sym = "<PRNTSCR>"; return GSYR_OK;
                    }

                }
			}
			
			// 0xE0 parsing
			switch (*buff)
			{
                case 0x12:                 
                    if (size == 3)
                    {
                        return GSYR_NEED_MORE_BYTES;
                    }
                    if (buff[1] == 0xE0 && buff[2] == 0x7C) 
                    {
                        *next = buff + 3; *sym = "<PRNTSCR>"; return GSYR_OK;
                    }
                    return GSYR_ERROR;
				case 0x1F: *next = buff + 1; *sym = "<LGUI>"; return GSYR_OK;
				case 0x14: *next = buff + 1; *sym = "<RCTRL>"; return GSYR_OK;
				case 0x27: *next = buff + 1; *sym = "<RGUI>"; return GSYR_OK;
				case 0x11: *next = buff + 1; *sym = "<RALT>"; return GSYR_OK;
				case 0x2F: *next = buff + 1; *sym = "<APPS>"; return GSYR_OK;
				case 0x70: *next = buff + 1; *sym = "<INSERT>"; return GSYR_OK;
				case 0x6C: *next = buff + 1; *sym = "<HOME>"; return GSYR_OK;
				case 0x7D: *next = buff + 1; *sym = "<PGUP>"; return GSYR_OK;
				case 0x71: *next = buff + 1; *sym = "<DEL>"; return GSYR_OK;
				case 0x69: *next = buff + 1; *sym = "<END>"; return GSYR_OK;
				case 0x7A: *next = buff + 1; *sym = "<PGDN>"; return GSYR_OK;
				case 0x75: *next = buff + 1; *sym = "<UROW>"; return GSYR_OK;
				case 0x6B: *next = buff + 1; *sym = "<LROW>"; return GSYR_OK;
				case 0x72: *next = buff + 1; *sym = "<DROW>"; return GSYR_OK;
				case 0x74: *next = buff + 1; *sym = "<RROW>"; return GSYR_OK;
				case 0x4A: *next = buff + 1; *sym = "/"; return GSYR_OK;
				case 0x5A: *next = buff + 1; *sym = "<EN>"; return GSYR_OK;
				case 0x37: *next = buff + 1; *sym = "<PWR>"; return GSYR_OK;
				case 0x3F: *next = buff + 1; *sym = "<SLEEP>"; return GSYR_OK;
				case 0x5E: *next = buff + 1; *sym = "<WAKE>"; return GSYR_OK;
				case 0x4D: *next = buff + 1; *sym = "<NTRACK>"; return GSYR_OK;
				case 0x15: *next = buff + 1; *sym = "<PTRACK>"; return GSYR_OK;
				case 0x3B: *next = buff + 1; *sym = "<STOP>"; return GSYR_OK;
				case 0x34: *next = buff + 1; *sym = "<PLAY/PAUSE>"; return GSYR_OK;
				case 0x23: *next = buff + 1; *sym = "<MUTE>"; return GSYR_OK;
				case 0x32: *next = buff + 1; *sym = "<VOLUP>"; return GSYR_OK;
				case 0x21: *next = buff + 1; *sym = "<VOLDOWN>"; return GSYR_OK;
				case 0x50: *next = buff + 1; *sym = "<MEDIASEL>"; return GSYR_OK;
				case 0x48: *next = buff + 1; *sym = "<MAIL>"; return GSYR_OK;
				case 0x2B: *next = buff + 1; *sym = "<CALC>"; return GSYR_OK;
				case 0x40: *next = buff + 1; *sym = "<MYPC>"; return GSYR_OK;
				case 0x10: *next = buff + 1; *sym = "<WWWSEARCH>"; return GSYR_OK;
				case 0x3A: *next = buff + 1; *sym = "<WWWHOME>"; return GSYR_OK;
				case 0x38: *next = buff + 1; *sym = "<WWWBACK>"; return GSYR_OK;
				case 0x30: *next = buff + 1; *sym = "<WWWFORWARD>"; return GSYR_OK;
				case 0x28: *next = buff + 1; *sym = "<WWWSTOP>"; return GSYR_OK;
				case 0x20: *next = buff + 1; *sym = "<WWWREFRESH>"; return GSYR_OK;
				case 0x18: *next = buff + 1; *sym = "<WWWFAV>"; return GSYR_OK;
			}
			return GSYR_ERROR;
		}
			
	}

	switch (*buff)
	{
		case 0x1C: *next = buff + 1; *sym = "A"; return GSYR_OK;
		case 0x32: *next = buff + 1; *sym = "B"; return GSYR_OK;
		case 0x21: *next = buff + 1; *sym = "C"; return GSYR_OK;
		case 0x23: *next = buff + 1; *sym = "D"; return GSYR_OK;
		case 0x24: *next = buff + 1; *sym = "E"; return GSYR_OK;
		case 0x2B: *next = buff + 1; *sym = "F"; return GSYR_OK;
		case 0x34: *next = buff + 1; *sym = "G"; return GSYR_OK;
		case 0x33: *next = buff + 1; *sym = "H"; return GSYR_OK;
		case 0x43: *next = buff + 1; *sym = "I"; return GSYR_OK;
		case 0x3B: *next = buff + 1; *sym = "J"; return GSYR_OK;
		case 0x42: *next = buff + 1; *sym = "K"; return GSYR_OK;
		case 0x4B: *next = buff + 1; *sym = "L"; return GSYR_OK;
		case 0x3A: *next = buff + 1; *sym = "M"; return GSYR_OK;
		case 0x31: *next = buff + 1; *sym = "N"; return GSYR_OK;
		case 0x44: *next = buff + 1; *sym = "O"; return GSYR_OK;
		case 0x4D: *next = buff + 1; *sym = "P"; return GSYR_OK;
		case 0x15: *next = buff + 1; *sym = "Q"; return GSYR_OK;
		case 0x2D: *next = buff + 1; *sym = "R"; return GSYR_OK;
		case 0x1B: *next = buff + 1; *sym = "S"; return GSYR_OK;
		case 0x2C: *next = buff + 1; *sym = "T"; return GSYR_OK;
		case 0x3C: *next = buff + 1; *sym = "U"; return GSYR_OK;
		case 0x2A: *next = buff + 1; *sym = "V"; return GSYR_OK;
		case 0x1D: *next = buff + 1; *sym = "W"; return GSYR_OK;
		case 0x22: *next = buff + 1; *sym = "X"; return GSYR_OK;
		case 0x35: *next = buff + 1; *sym = "Y"; return GSYR_OK;
		case 0x1A: *next = buff + 1; *sym = "Z"; return GSYR_OK;
		case 0x45: *next = buff + 1; *sym = "0"; return GSYR_OK;
		case 0x16: *next = buff + 1; *sym = "1"; return GSYR_OK;
		case 0x1E: *next = buff + 1; *sym = "2"; return GSYR_OK;
		case 0x26: *next = buff + 1; *sym = "3"; return GSYR_OK;
		case 0x25: *next = buff + 1; *sym = "4"; return GSYR_OK;
		case 0x2E: *next = buff + 1; *sym = "5"; return GSYR_OK;
		case 0x36: *next = buff + 1; *sym = "6"; return GSYR_OK;
		case 0x3D: *next = buff + 1; *sym = "7"; return GSYR_OK;
		case 0x3E: *next = buff + 1; *sym = "8"; return GSYR_OK;
		case 0x46: *next = buff + 1; *sym = "9"; return GSYR_OK;
		case 0x0E: *next = buff + 1; *sym = "`"; return GSYR_OK;
		case 0x4E: *next = buff + 1; *sym = "-"; return GSYR_OK;
		case 0x55: *next = buff + 1; *sym = "="; return GSYR_OK;
		case 0x5D: *next = buff + 1; *sym = "\\"; return GSYR_OK;
		case 0x66: *next = buff + 1; *sym = "<BKSP>"; return GSYR_OK;
		case 0x29: *next = buff + 1; *sym = "<SPACE>"; return GSYR_OK;
		case 0x0D: *next = buff + 1; *sym = "<TAB>"; return GSYR_OK;
		case 0x58: *next = buff + 1; *sym = "<CAPS>"; return GSYR_OK;
		case 0x12: *next = buff + 1; *sym = "<LSHIFT>"; return GSYR_OK;
		case 0x14: *next = buff + 1; *sym = "<LCTRL>"; return GSYR_OK;
		case 0x11: *next = buff + 1; *sym = "<LALT>"; return GSYR_OK;
		case 0x59: *next = buff + 1; *sym = "<RSHIFT>"; return GSYR_OK;
		case 0x5A: *next = buff + 1; *sym = "<ENTER>"; return GSYR_OK;
		case 0x76: *next = buff + 1; *sym = "<ESC>"; return GSYR_OK;
		case 0x05: *next = buff + 1; *sym = "<F1>"; return GSYR_OK;
		case 0x06: *next = buff + 1; *sym = "<F2>"; return GSYR_OK;
		case 0x04: *next = buff + 1; *sym = "<F3>"; return GSYR_OK;
		case 0x0C: *next = buff + 1; *sym = "<F4>"; return GSYR_OK;
		case 0x03: *next = buff + 1; *sym = "<F5>"; return GSYR_OK;
		case 0x0B: *next = buff + 1; *sym = "<F6>"; return GSYR_OK;
		case 0x83: *next = buff + 1; *sym = "<F7>"; return GSYR_OK;
		case 0x0A: *next = buff + 1; *sym = "<F8>"; return GSYR_OK;
		case 0x01: *next = buff + 1; *sym = "<F9>"; return GSYR_OK;
		case 0x09: *next = buff + 1; *sym = "<F10>"; return GSYR_OK;
		case 0x78: *next = buff + 1; *sym = "<F11>"; return GSYR_OK;
		case 0x07: *next = buff + 1; *sym = "<F12>"; return GSYR_OK;
		case 0x7E: *next = buff + 1; *sym = "<SCROLL>"; return GSYR_OK;
		case 0x54: *next = buff + 1; *sym = "["; return GSYR_OK;
		case 0x77: *next = buff + 1; *sym = "<NUM>"; return GSYR_OK;
		case 0x7C: *next = buff + 1; *sym = "*"; return GSYR_OK;
		case 0x7B: *next = buff + 1; *sym = "-"; return GSYR_OK;
		case 0x79: *next = buff + 1; *sym = "+"; return GSYR_OK;
		case 0x71: *next = buff + 1; *sym = "."; return GSYR_OK;
		case 0x70: *next = buff + 1; *sym = "0"; return GSYR_OK;
		case 0x69: *next = buff + 1; *sym = "1"; return GSYR_OK;
		case 0x72: *next = buff + 1; *sym = "2"; return GSYR_OK;
		case 0x7A: *next = buff + 1; *sym = "3"; return GSYR_OK;
		case 0x6B: *next = buff + 1; *sym = "4"; return GSYR_OK;
		case 0x73: *next = buff + 1; *sym = "5"; return GSYR_OK;
		case 0x74: *next = buff + 1; *sym = "6"; return GSYR_OK;
		case 0x6C: *next = buff + 1; *sym = "7"; return GSYR_OK;
		case 0x75: *next = buff + 1; *sym = "8"; return GSYR_OK;
		case 0x7D: *next = buff + 1; *sym = "9"; return GSYR_OK;
		case 0x5B: *next = buff + 1; *sym = "]"; return GSYR_OK;
		case 0x4C: *next = buff + 1; *sym = ";"; return GSYR_OK;
		case 0x52: *next = buff + 1; *sym = "'"; return GSYR_OK;
		case 0x41: *next = buff + 1; *sym = ","; return GSYR_OK;
		case 0x49: *next = buff + 1; *sym = "."; return GSYR_OK;
		case 0x4A: *next = buff + 1; *sym = "/"; return GSYR_OK;
	}
	
	return GSYR_ERROR;
}


    /*
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, false);
    uart_set_irq_enables(UART_ID, false, false);

    uart_puts(UART_ID, "\r\nokhi started!\r\n"); 



void core1_main()
{
    #ifdef ENABLE_GLITCH_DETECTOR
    gpio_set_irq_enabled_with_callback(DAT_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(CLK_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    #endif // ENABLE_GLITCH_DETECTOR

    sleep_ms(2000);
    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);
    sleep_ms(2000);

    gpio_set_irq_enabled_with_callback(ESP_RESET_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    uart_init(uart0, 74880);
    gpio_set_function(16, GPIO_FUNC_UART);
    gpio_set_function(17, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(uart0, false);
    uart_set_irq_enables(uart0, false, false);

    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);
    gpio_pull_up(ELOG_SLAVEREADY_GPIO);
    for (int i = 0; i < 3000; i++) 
    { 
        if (gpio_get(ELOG_SLAVEREADY_GPIO))
        {
            printf("ESP slave not ready yet... %d\r\n", i);
        }
        else
        {
            break;
        }
        tight_loop_contents();
    }
    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    unsigned int read_index = 0;
    unsigned int total_packets_sended = 0;
    unsigned int g = 0;
    unsigned int z = 90000000 + 1;
    unsigned int last_sended = 0;
    while (1) 
    {
        static unsigned char line[32] = {0};
        while (read_index != write_index) 
        {
            sprintf(line, "%s   \r\n", &(ringbuff[read_index++ % (RING_BUFF_MAX_ENTRIES - 1)][32]));
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);
            while (!gpio_get(ELOG_SLAVEREADY_GPIO)) 
            {
                tight_loop_contents();
            }
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
            my_spi_write_blocking(line, strlen(line));
            printf("%s", line);
            total_packets_sended++;
        }
        if (last_sended != total_packets_sended && g++ > 20000000)
        {
            z = 0;
            g = 0;
            last_sended = total_packets_sended;
            sprintf(line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen(line) + 1);
            puts(line);
        }
        else if (z++ > 90000000)
        {
            z = 0;
            g = 0;
            sprintf(line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen(line) + 1);
            puts(line);
        }
        #ifdef ENABLE_GLITCH_DETECTOR
        for (int i = 0; i < 2; i++) 
        {
            if (pin_states_glitch[i].signal_detected) 
            {
                printf("Possible glitch %s t:0x%08X %lldus\r\n", i == 0 ? "DAT" : "CLK", to_ms_since_boot(pin_states_glitch[i].last_rising_edge), pin_states_glitch[i].pulse_width_us);
                pin_states_glitch[i].signal_detected = false;  
            }
        }
        #endif // ENABLE_GLITCH_DETECTOR
    }
}


#define UART_PURE_SOFT_TX_PIN_BAUD_RATE 4800 // MAX POSSSIBLE SPEED USING GPIO28 (CHIP_PU ESP CAPACITOR IN PCB)
#define UART_PURE_SOFT_TX_PIN 28 // CHIP_PU ESP, SO ESP IS UNUSABLE

void uart_pure_soft_init(void) 
{
    gpio_init(UART_PURE_SOFT_TX_PIN);
    gpio_set_dir(UART_PURE_SOFT_TX_PIN, GPIO_OUT);
    gpio_put(UART_PURE_SOFT_TX_PIN, 1);
    sleep_ms(500); 
}

void uart_pure_soft_putc(char c) 
{
    uint32_t bit_time_us = 1000000 / UART_PURE_SOFT_TX_PIN_BAUD_RATE;
    absolute_time_t t = get_absolute_time();

    // Disable interrupts to maintain timing accuracy
    uint32_t save = save_and_disable_interrupts();

    // Start bit (Low)
    gpio_put(UART_PURE_SOFT_TX_PIN, 0);
    t = delayed_by_us(t, bit_time_us);
    busy_wait_until(t);

    // Data bits (LSB first)
    for (int i = 0; i < 8; i++) {
        gpio_put(UART_PURE_SOFT_TX_PIN, (c >> i) & 0x1);
        t = delayed_by_us(t, bit_time_us);
        busy_wait_until(t);
    }

    // Stop bit (High)
    gpio_put(UART_PURE_SOFT_TX_PIN, 1);
    t = delayed_by_us(t, bit_time_us);
    busy_wait_until(t);

    // Restore interrupts
    restore_interrupts(save);
}

void uart_pure_soft_puts(const char* str) {
    while (*str) {
        uart_pure_soft_putc(*str++);
    }
}

void test_pin_mirror(void)
{
    int last_dat_state = -1;
    int last_clk_state = -1;

    int current_dat_state;
    int current_clk_state;
    int current_dat_inverted_state;
    int current_clk_inverted_state;

    for (int i = 0; ; i++) 
    {
        current_dat_state = gpio_get(DAT_GPIO);
        current_clk_state = gpio_get(CLK_GPIO);
        current_dat_inverted_state = gpio_get(DAT_INVERTED_GPIO);
        current_clk_inverted_state = gpio_get(CLK_INVERTED_GPIO);

        if (current_dat_state != last_dat_state || current_clk_state != last_clk_state) 
        {
            last_dat_state = current_dat_state;
            last_clk_state = current_clk_state;

            puts("--------------------");
            printf("DAT_GPIO: %d\r\n", current_dat_state);
            printf("DAT_INVERTED_GPIO: %d\r\n", current_dat_inverted_state);
            puts(" ");
            printf("CLK_GPIO: %d\r\n", current_clk_state);
            printf("CLK_INVERTED_GPIO: %d\r\n", current_clk_inverted_state);
        }
    }
}

void test_glitch_detector(uint kbd_glitch_sm)
{
    int i = 0;
    while (1) 
    {
        if (pio_sm_get_rx_fifo_level(pio1, kbd_glitch_sm) > 0) 
        {
            uint32_t data = pio_sm_get(pio1, kbd_glitch_sm);
            printf("Glitch detected n[%d]: %d\r\n", i++, data);
        }
    }
}

    
    // INIT MIRROR PINS
    gpio_init(DAT_INVERTED_GPIO);
    gpio_set_dir(DAT_INVERTED_GPIO, GPIO_OUT);
    gpio_put(DAT_INVERTED_GPIO, false);
    gpio_init(CLK_INVERTED_GPIO);
    gpio_set_dir(CLK_INVERTED_GPIO, GPIO_OUT);
    gpio_put(CLK_INVERTED_GPIO, false);
    pio_gpio_init(pio0, DAT_INVERTED_GPIO);
    pio_gpio_init(pio0, CLK_INVERTED_GPIO);

    // INIT REVERSE PINS
    gpio_init(DAT_MIRROR_GPIO);
    gpio_set_dir(DAT_MIRROR_GPIO, GPIO_OUT);
    gpio_put(DAT_MIRROR_GPIO, false);
    gpio_init(CLK_MIRROR_GPIO);
    gpio_set_dir(CLK_MIRROR_GPIO, GPIO_OUT);
    gpio_put(CLK_MIRROR_GPIO, false);
    pio_gpio_init(pio0, DAT_MIRROR_GPIO);
    pio_gpio_init(pio0, CLK_MIRROR_GPIO);

    //sleep_ms(6000);

    
    // MIRROR AND REVERSE PINS
    int kbd_prm_sm = pio_claim_unused_sm(pio0, true);
    uint offset_kbd_prm = pio_add_program(pio0, &two_pin_mirror_reverse_inverter_program);
    pio_sm_set_consecutive_pindirs(pio0, kbd_prm_sm, DAT_GPIO, 2, false);
    pio_sm_set_consecutive_pindirs(pio0, kbd_prm_sm, DAT_MIRROR_GPIO, 2, true);
    pio_sm_config c_kbd_prm = two_pin_mirror_reverse_inverter_program_get_default_config(offset_kbd_prm);
    sm_config_set_in_pins(&c_kbd_prm, DAT_GPIO);
    sm_config_set_in_shift(&c_kbd_prm, true, false, 0);
    sm_config_set_out_pins(&c_kbd_prm, DAT_MIRROR_GPIO, 2);
    // Must run at maximum possible frequency
    sm_config_set_clkdiv(&c_kbd_prm, 1.0);
    pio_sm_init(pio0, kbd_prm_sm, offset_kbd_prm, &c_kbd_prm);
    pio_sm_set_enabled(pio0, kbd_prm_sm, false);
    pio_sm_clear_fifos(pio0, kbd_prm_sm);
    pio_sm_restart(pio0, kbd_prm_sm);
    pio_sm_clkdiv_restart(pio0, kbd_prm_sm);
    pio_sm_set_enabled(pio0, kbd_prm_sm, true);
    pio_sm_exec(pio0, kbd_prm_sm, pio_encode_jmp(offset_kbd_prm));

    /*

    // MIRROR AND INVERT PINS
    int kbd_pim_sm = pio_claim_unused_sm(pio0, true);
    uint offset_kbd_pim = pio_add_program(pio0, &pins_invert_mirror_program);
    pio_sm_set_consecutive_pindirs(pio0, kbd_pim_sm, DAT_GPIO, 2, false);
    pio_sm_set_consecutive_pindirs(pio0, kbd_pim_sm, CLK_INVERTED_GPIO, 2, true);
    pio_sm_config c_kbd_pim = pins_invert_mirror_program_get_default_config(offset_kbd_pim);
    sm_config_set_in_pins(&c_kbd_pim, DAT_GPIO);
    sm_config_set_out_pins(&c_kbd_pim, CLK_INVERTED_GPIO, 2);
    // Must run at maximum possible frequency
    sm_config_set_clkdiv(&c_kbd_pim, 1.0);
    pio_sm_init(pio0, kbd_pim_sm, offset_kbd_pim, &c_kbd_pim);
    pio_sm_set_enabled(pio0, kbd_pim_sm, false);
    pio_sm_clear_fifos(pio0, kbd_pim_sm);
    pio_sm_restart(pio0, kbd_pim_sm);
    pio_sm_clkdiv_restart(pio0, kbd_pim_sm);
    pio_sm_set_enabled(pio0, kbd_pim_sm, true);
    pio_sm_exec(pio0, kbd_pim_sm, pio_encode_jmp(offset_kbd_pim));
    // test_pin_mirror();

    // GLITCH DETECTOR FAST RISE
    uint kbd_glitch_sm = pio_claim_unused_sm(pio1, true);
    uint offset_kbd_glitch = pio_add_program(pio1, &glitch_det_fast_rise_program);
    pio_sm_set_consecutive_pindirs(pio1, kbd_glitch_sm, CLK_GPIO, 1, false);
    pio_sm_config c_kbd_glitch = glitch_det_fast_rise_program_get_default_config(offset_kbd_glitch);
    sm_config_set_in_pins(&c_kbd_glitch, CLK_GPIO);
    sm_config_set_in_shift(&c_kbd_glitch, false, true, 1);
    sm_config_set_jmp_pin(&c_kbd_glitch, CLK_GPIO);
    float div_kbd_glitch = (float)clock_get_hz(clk_sys) / 4000000.0; // 4 MHz
    sm_config_set_clkdiv(&c_kbd_glitch, div_kbd_glitch);    
    pio_sm_init(pio1, kbd_glitch_sm, offset_kbd_glitch, &c_kbd_glitch);
    pio_sm_set_enabled(pio1, kbd_glitch_sm, false);
    pio_sm_clear_fifos(pio1, kbd_glitch_sm);
    pio_sm_restart(pio1, kbd_glitch_sm);
    pio_sm_clkdiv_restart(pio1, kbd_glitch_sm);
    pio_sm_set_enabled(pio1, kbd_glitch_sm, true);
    pio_sm_exec(pio1, kbd_glitch_sm, pio_encode_jmp(offset_kbd_glitch));
    //test_glitch_detector(kbd_glitch_sm);

; PIN 0 & JMP PIN = same PIN
; 2 MHz, each cycle 0.5 us, so 2 instructions = 1 us
; a pulse less than ~15 us = glitch
.wrap_target
start:
	set x, 15 
	set y, !NULL
	wait 0 PIN 0
	wait 1 PIN 0

loop_glitch:
	jmp pin next_iter
	jmp glitch_detected
next_iter:
	jmp x-- loop_glitch
	
	jmp x!=y glitch_detected
	jmp start
glitch_detected:
	mov isr, x
	push noblock
.wrap



    //set_sys_clock_khz(250000, true);
    
    uart_pure_soft_init();
    while (1)
    {
        uart_pure_soft_puts("Hello, world!\r\n");
        sleep_ms(1);
    }


Arduino tester idle:

const int PINCLK = 12;
const int PINDAT = 13; // DAT & ON BOARD LED

void setup() {
  pinMode(PINCLK, OUTPUT);
  pinMode(PINDAT, OUTPUT);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(80);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  delay(3000);
  
  digitalWrite(PINCLK, LOW);
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(PINDAT, HIGH);
    delay(500);
    digitalWrite(PINDAT, LOW);
    delay(500);
  }

  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);

  delayMicroseconds(100);

  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delay(5000);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);

  delayMicroseconds(100);
}

void loop() {
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
}
*/

/* 
Arduino tester inhibited:

const int PINCLK = 12;
const int PINDAT = 13; // DAT & ON BOARD LED

void setup() {
  pinMode(PINCLK, OUTPUT);
  pinMode(PINDAT, OUTPUT);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(80);
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, HIGH);
  delayMicroseconds(100);
  digitalWrite(PINDAT, LOW);
  digitalWrite(PINCLK, LOW);
  delayMicroseconds(100);
  delay(3000);
  
  digitalWrite(PINCLK, HIGH);
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(PINDAT, HIGH);
    delay(500);
    digitalWrite(PINDAT, LOW);
    delay(500);
  }

  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);

  delayMicroseconds(100);

  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, HIGH);
  delay(5000);
}

void loop() {
  digitalWrite(PINDAT, HIGH);
  digitalWrite(PINCLK, LOW);
}
*/



/*
unsigned char buff[512] = {0};
unsigned char* next = buff;
unsigned int i = 0;
bool press = true;
char* sym;

while (1)
{
    if (!pio_sm_is_rx_fifo_empty(pio1, kbd_h2d_sm))
    {
        uint8_t byte = *((io_rw_8*)&pio1->rxf[kbd_h2d_sm] + 3);
        printf("\r\nH:0x%02X", byte);
    }
    if (!pio_sm_is_rx_fifo_empty(pio0, kbd_sm))
    {
        uint8_t byte = *((io_rw_8*)&pio0->rxf[kbd_sm] + 3);
        printf("\r\nD:0x%02X ", byte);
        buff[i++] = byte;
        if (get_sym(next, 8, &next, &press, &sym) == GSYR_OK)
        {
            printf("\r\nsym: %s (%s)\r\n", sym, press ? "press" : "release");
        }
        else if (buff + i > next + 8)
        {
            next++;
        }
        
    }
}

while(1)
{
    char c = kbd_getc();
    uart_putc(UART_ID, c);
}


void start_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, false);
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
}

void stop_device_to_host_sm(void)
{
    gpio_put(AUX_D2H_JMP_GPIO, true);
    //pio_sm_restart(pio0, kbd_sm);
    //pio_sm_clkdiv_restart(pio0, kbd_sm);
    pio_sm_exec(pio0, kbd_sm, pio_encode_jmp(offset_kbd));
    pio_sm_clear_fifos(pio0, kbd_sm);
}

void start_host_to_device_sm(void)
{
    gpio_put(AUX_H2D_JMP_GPIO, false);
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
    pio_sm_clear_fifos(pio1, kbd_h2d_sm);
}

void stop_host_to_device_sm(void)
{
    gpio_put(AUX_H2D_JMP_GPIO, true);
    //pio_sm_restart(pio1, kbd_h2d_sm);
    //pio_sm_clkdiv_restart(pio1, kbd_h2d_sm);
    pio_sm_exec(pio1, kbd_h2d_sm, pio_encode_jmp(offset_kbd_h2d));
    pio_sm_clear_fifos(pio1, kbd_h2d_sm);
}


// IRQ0: Inhibited communication detected
// IRQ1: No Host Request-to-Send detected after inhibiting communication
void pio0_irq(void) 
{
    //printf("PIO0 IRQ!\r\n");
    if (pio0_hw->irq & 1) 
    {
        //printf("PIO0 IRQ & 1: %d\r\n", inhnr++);
        pio0_hw->irq = 1;
        stop_device_to_host_sm();
        start_host_to_device_sm();
    } 
    else if (pio0_hw->irq & 2) 
    {
        //printf("PIO0 IRQ & 2: %d\r\n", inhnr++);
        pio0_hw->irq = 2;
        start_device_to_host_sm();
        stop_host_to_device_sm();
    }
}

// IRQ0: IDLE DETECTED, CLOCK is HIGH + DAT is HIGH for at least 100 microseconds
// IRQ1: NOT IDLE DETECTED
void pio1_irq(void) 
{
    //printf("PIO1 IRQ!\r\n");
    if (pio1_hw->irq & 1) 
    {
        //printf("PIO1 IRQ & 1: %d\r\n", inidle++);
        pio1_hw->irq = 1;
        start_device_to_host_sm();
        stop_host_to_device_sm();
    } 
    else if (pio1_hw->irq & 2) 
    {
        //printf("PIO1 IRQ & 2: %d\r\n", inidle++);
        pio1_hw->irq = 2;
    }
}

// COMMENT BELOW LINE TO DISABLE GLITCH DETECTION
//#define ENABLE_GLITCH_DETECTOR 
#define RING_BUFF_MAX_ENTRIES 800
volatile static unsigned int write_index = 0;
volatile static char ringbuff[RING_BUFF_MAX_ENTRIES][32];

#ifdef ENABLE_GLITCH_DETECTOR
#define GLITCH_DETECTION_THRESHOLD 15
typedef struct 
{
    volatile bool signal_detected;
    volatile absolute_time_t last_rising_edge;
    volatile int64_t pulse_width_us;
} pin_state_glitch_t;
volatile pin_state_glitch_t pin_states_glitch[2];  
void gpio_callback(uint gpio, uint32_t events) 
{
    int pin_index = -1;
    if (gpio == DAT_GPIO) 
    {
        pin_index = 0;  
    } 
    else if (gpio == CLK_GPIO) 
    {
        pin_index = 1; 
    }
    else 
    {
        return;
    }
    if (pin_states_glitch[pin_index].signal_detected)
    {
        return;
    }
    if (events & GPIO_IRQ_EDGE_RISE) 
    {
        pin_states_glitch[pin_index].last_rising_edge = get_absolute_time();
    } 
    else if (events & GPIO_IRQ_EDGE_FALL) 
    {
        pin_states_glitch[pin_index].pulse_width_us = absolute_time_diff_us(pin_states_glitch[pin_index].last_rising_edge, get_absolute_time());
        if (pin_states_glitch[pin_index].pulse_width_us < GLITCH_DETECTION_THRESHOLD) 
        {
            pin_states_glitch[pin_index].signal_detected = true;
        }
    }
}
#endif // ENABLE_GLITCH_DETECTOR


#define CLK_INVERTED_GPIO 0
#define DAT_INVERTED_GPIO 1

#define DAT_MIRROR_GPIO 2
#define CLK_MIRROR_GPIO 3


// Upper-Case ASCII codes by keyboard-code index, 16 elements per row
static const uint8_t lower[] = 
{
    0,  0,   0,   0,   0,   0,   0,   0,  0,  0,   0,   0,   0,   TAB, '`', 0,
    0,  0,   0,   0,   0,   'q', '1', 0,  0,  0,   'z', 's', 'a', 'w', '2', 0,
    0,  'c', 'x', 'd', 'e', '4', '3', 0,  0,  ' ', 'v', 'f', 't', 'r', '5', 0,
    0,  'n', 'b', 'h', 'g', 'y', '6', 0,  0,  0,   'm', 'j', 'u', '7', '8', 0,
    0,  ',', 'k', 'i', 'o', '0', '9', 0,  0,  '.', '/', 'l', ';', 'p', '-', 0,
    0,  0,   '\'',0,   '[', '=', 0,   0,  0,  0,   LF,  ']', 0,   '\\',0,   0,
    0,  0,   0,   0,   0,   0,   BS,  0,  0,  0,   0,   0,   0,   0,   0,   0,
    0,  0,   0,   0,   0,   0,   ESC, 0,  0,  0,   0,   0,   0,   0,   0,   0 
};

// Upper-Case ASCII codes by keyboard-code index
static const uint8_t upper[] = 
{
    0,  0,   0,   0,   0,   0,   0,   0,  0,  0,   0,   0,   0,   TAB, '~', 0,
    0,  0,   0,   0,   0,   'Q', '!', 0,  0,  0,   'Z', 'S', 'A', 'W', '@', 0,
    0,  'C', 'X', 'D', 'E', '$', '#', 0,  0,  ' ', 'V', 'F', 'T', 'R', '%', 0,
    0,  'N', 'B', 'H', 'G', 'Y', '^', 0,  0,  0,   'M', 'J', 'U', '&', '*', 0,
    0,  '<', 'K', 'I', 'O', ')', '(', 0,  0,  '>', '?', 'L', ':', 'P', '_', 0,
    0,  0,   '"', 0,   '{', '+', 0,   0,  0,  0,   LF,  '}', 0,   '|', 0,   0,
    0,  0,   0,   0,   0,   0,   BS,  0,  0,  0,   0,   0,   0,   0,   0,   0,
    0,  0,   0,   0,   0,   0,   ESC, 0,  0,  0,   0,   0,   0,   0,   0,   0
};

volatile static uint8_t release; 
volatile static uint8_t shift;   
volatile static uint8_t ascii;  

#define BS 0x8
#define TAB 0x9
#define LF 0xA
#define ESC 0x1B


USB GARB


   /*
    TEST USB SWITCH AFTER ENABLE
    Just run this code and after a few blinks, connect +3v3 TO DP_INDEX
    THE LED WILL STOP BLINKING

    gpio_init(DP_INDEX);
    gpio_set_dir(DP_INDEX, GPIO_IN);
    gpio_pull_down(DP_INDEX);
    sleep_ms(100);
    while (gpio_get(DP_INDEX) == 0) 
    {
        blink_led(4);
    }
    */
	
	
	
    /*
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, false);
    uart_set_irq_enables(UART_ID, false, false);

    uart_puts(UART_ID, "\r\nokhi started!\r\n"); 
    */

    /*
    TEST USB SWITCH BEFORE ENABLE
    Just run this code and after a few blinks, connect +3v3 TO DP_INDEX
    THE LED WILL NEVER STOP BLINKING

    gpio_init(DP_INDEX);
    gpio_set_dir(DP_INDEX, GPIO_IN);
    gpio_pull_down(DP_INDEX);
    sleep_ms(100);
    while (gpio_get(DP_INDEX) == 0) 
    {
        blink_led(4);
    }
    
	
	
void oldcore1_main()
{
    sleep_ms(2000);
    gpio_init(ESP_RESET_GPIO);
    gpio_set_dir(ESP_RESET_GPIO, GPIO_IN);
    gpio_pull_up(ESP_RESET_GPIO);
    sleep_ms(2000);

    gpio_set_irq_enabled_with_callback(ESP_RESET_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    uart_init(uart0, 74880);
    gpio_set_function(16, GPIO_FUNC_UART);
    gpio_set_function(17, GPIO_FUNC_UART);
    // UART 8N1: 1 start bit, 8 data bits, no parity bit, 1 stop bit
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(uart0, false);
    uart_set_irq_enables(uart0, false, false);

    gpio_init(ELOG_SLAVEREADY_GPIO);
    gpio_set_dir(ELOG_SLAVEREADY_GPIO, GPIO_IN);
    gpio_pull_up(ELOG_SLAVEREADY_GPIO);
    for (int i = 0; i < 3000; i++) 
    { 
        if (gpio_get(ELOG_SLAVEREADY_GPIO))
        {
            printf("ESP slave not ready yet... %d\r\n", i);
        }
        else
        {
            break;
        }
        tight_loop_contents();
    }
    gpio_init(EBOOT_MASTERDATAREADY_GPIO);
    gpio_set_dir(EBOOT_MASTERDATAREADY_GPIO, GPIO_OUT);
    gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);

    unsigned int read_index = 0;
    unsigned int total_packets_sended = 0;
    unsigned int g = 0;
    unsigned int z = 90000000 + 1;
    unsigned int last_sended = 0;
    while (1) 
    {
        static unsigned char line[32] = {0};
        while (read_index != write_index) 
        {
            sprintf((char*)line, "%s   \r\n", &(ringbuff[read_index++ % (RING_BUFF_MAX_ENTRIES - 1)][32]));
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, true);
            while (!gpio_get(ELOG_SLAVEREADY_GPIO)) 
            {
                tight_loop_contents();
            }
            gpio_put(EBOOT_MASTERDATAREADY_GPIO, false);
            my_spi_write_blocking(line, strlen((char*)line));
            printf("%s", (char*)line);
            total_packets_sended++;
        }
        if (last_sended != total_packets_sended && g++ > 20000000)
        {
            z = 0;
            g = 0;
            last_sended = total_packets_sended;
            sprintf((char*)line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen((char*)line) + 1);
            puts((char*)line);
        }
        else if (z++ > 90000000)
        {
            z = 0;
            g = 0;
            sprintf((char*)line, "HWv%s packets sended: 0x%x", hwver_name, total_packets_sended);
            uart_write_blocking(uart0, line, strlen((char*)line) + 1);
            puts((char*)line);
        }
    }
}


/*
  PICO_DEFAULT_UART=1
	PICO_DEFAULT_UART_TX_PIN=4
	PICO_DEFAULT_UART_RX_PIN=5
  PICO_DEFAULT_UART_BAUD_RATE=921600
  PICO_FLASH_SIZE_BYTES=16777216
*/
	*/
	
	


*/

