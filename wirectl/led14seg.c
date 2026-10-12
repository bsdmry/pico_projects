#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "led14seg.h"

volatile bool blink_flag = true;
static struct repeating_timer blink_timer;

static bool blink_timer_callback(__unused struct repeating_timer *timer) {
    blink_flag = !blink_flag;
    return true;
}

bool led14seg_blink_timer_init(void) {
    blink_flag = true;
    return add_repeating_timer_ms(-500, blink_timer_callback, NULL,
                                  &blink_timer);
}

/** Convert one supported ASCII character to a segment bitmap. */
static uint16_t bitmap(char ch) {
    unsigned c = (unsigned char)ch;
    return c >= 32u && c <= 126u ? syms[c - 32u] : 0x0728;
}

/* Normalize a zero-based, inclusive [start_pos, stop_pos] display range. */
uint8_t normalize_range(uint8_t *start_pos, uint8_t *stop_pos) {
    if (*start_pos >= LED14SEG_CHARACTER_COUNT) {
        return 0;
    }
    if (*stop_pos >= LED14SEG_CHARACTER_COUNT) {
        *stop_pos = (uint8_t)(LED14SEG_CHARACTER_COUNT - 1u);
    }
    if (*stop_pos < *start_pos) {
        return 0;
    }
    return (uint8_t)(*stop_pos - *start_pos + 1u);
}

void fill_range(led14seg_display_t *display, uint8_t start_pos, uint8_t stop_pos, uint16_t bitmap) {
	uint8_t pos;
  	for (pos = start_pos; pos <= stop_pos; ++pos) {
     	display->display_buffer[pos] = bitmap;
    }
}

void write_error_field(led14seg_display_t *display, uint8_t start_pos, uint8_t stop_pos){
      fill_range(display, start_pos, stop_pos, HASH_SYM);
}

static void send_char(spi_inst_t *spi, uint cs, uint16_t segments, bool dot, uint8_t position) {
    uint8_t bytes[4] = {
		(uint8_t)((dot ? 1u : 0u) ^ 0xffu), 	//byte 0 - dot segment, 	  	with inversion
        (uint8_t)((segments & 0xffu) ^ 0xffu),  //byte 1 - symbol, low byte,	with inversion
		(uint8_t)((segments >> 8) ^ 0xffu), 	//byte 2 - symbol, high byte,	with inversion
		position};								//byte 3 - position				without inversion
    gpio_put(cs, 0);
	spi_write_blocking(spi, bytes, 4); 
	gpio_put(cs, 1);
}

void led14seg_init(led14seg_display_t *display, spi_inst_t *spi,
                   uint sck, uint tx, uint cs) {
    spi_init(spi, 2250000u);
    spi_set_format(spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(sck, GPIO_FUNC_SPI); gpio_set_function(tx, GPIO_FUNC_SPI);
    gpio_init(cs); gpio_set_dir(cs, GPIO_OUT); gpio_put(cs, 1);
    display->spi = spi;
    display->cs_pin = cs;
    display->dot_bitmap = 0;
    display->blink_bitmap = 0;
    display->scan_position = 0;
    for (unsigned i = 0; i < LED14SEG_CHARACTER_COUNT; ++i)
        display->display_buffer[i] = bitmap(' ');
}

void led14seg_set_voltage(led14seg_display_t *display, uint16_t value) {
	write_float(display, (float)value / 100.0f, 0, 3, 2, 2, 0, 0, 1);
	write_string(display, " V", 2, 4, 5);
}
void led14seg_set_current(led14seg_display_t *display, uint16_t value) {
	write_float(display, (float)value / 1000.0f, 0, 3, 2, 2, 0, 0, 1);
	write_string(display, " A", 2, 4, 5);
}

void redraw_display(led14seg_display_t *display){
	uint8_t pos = 0x80;
	bool phase = blink_flag;
    for (uint8_t s = 0; s < LED14SEG_CHARACTER_COUNT; s++){
		bool blink_enabled = ((display->blink_bitmap >> s) & 0x1u) != 0u;
		bool show_symbol = !blink_enabled || phase;
     send_char(display->spi, display->cs_pin,
			show_symbol ? display->display_buffer[s] : SPACE_SYM,
			show_symbol && (((display->dot_bitmap >> s) & 0x1u) != 0u),
			pos >> s);
    }
}

  /** @brief Enables blinking for specific symbols
  Every bit in the bitmask represents symbol position.
  If blink_on param is True, each '1' bit in the bitmask represents
  blinking symbol and '0' - no change, if blink_on is False each '1' bit
  represents liting symbol and '0' - no change
  
  @param[inout] *display led14seg_display_t Display object
  @param[in] blink_on bool blink Is bitmask represents enable blinking or disable
  @param[in] blink_bitmask uint8_t modification bitmask  
*/
void blink_segment(led14seg_display_t *display, bool blink_on, uint8_t blink_bitmask){
	if (display == NULL) {
		return;
	}
	if (blink_on) {
		display->blink_bitmap |= blink_bitmask;
	} else {
		display->blink_bitmap &= (uint8_t)~blink_bitmask;
	}
}

  /** @brief Converts a string to the bitmap array for LED symbols
  Takes a string, and create array of bitmaps. If str_len is more
  than (stop_pos - start_pos + 1), writes only characters in this range,
  if str_len is less then place on LED, fill left space using space symbols
  
  @param[inout] *display led14seg_display_t Display object
  @param[in] *str Character. ASCII string for display
  @param[in] str_len Unsigned int8. How much symbol from the string to display
  @param[in] start_pos Unsigned int8. Zero-based first buffer index
  @param[in] stop_pos Unsigned int8. Zero-based last buffer index, inclusive
  
  */
  void write_string(led14seg_display_t *display, const char *str, uint8_t str_len, uint8_t start_pos, uint8_t stop_pos){
      uint8_t width;
      uint8_t count;
      uint8_t dst;
      uint8_t src;
  
      if ((display == 0) || (str == 0)) { return; }
      width = normalize_range(&start_pos, &stop_pos);
      if (width == 0) { return; }
  
      count = (str_len < width) ? str_len : width;
      fill_range(display, start_pos, stop_pos, SPACE_SYM);
  
      /* Right-align a short string in the selected field. */
      dst = (uint8_t)(stop_pos + 1u - count);
      for (src = 0; src < count; ++src) {
          display->display_buffer[(uint8_t)(dst + src)] = bitmap(str[src]);
      }
  }

/** @brief Writes a float value on LED display
  Takes a float value, and displays it on the display according 
  to the options.
  
  @param[inout] *display led14seg_display_t Display object
  @param[in] value Float. Value for display
  @param[in] start_pos Unsigned int8. Zero-based first buffer index
  @param[in] stop_pos Unsigned int8. Zero-based last buffer index, inclusive
  @param[in] int_size Unsigned int8. Digits number for a interger part
  @param[in] frac_size Unsigned int8. Digits number for a fractional part
  @param[in] is_signed Unsigned int8. Reserve one symbol place for sign. 
      If the input value is negative, and this option is 0 - display as positive
  @param[in] show_plus_sign Unsigned int8. If option is 1, display "+" for 
      positive values, else display space symbol
  @param[in] draw_zeros Unsigned int8. Draw leading zeros for the integer part
  */
  
  void write_float(led14seg_display_t *display, float value,
      uint8_t start_pos, uint8_t stop_pos,
      uint8_t int_size, uint8_t frac_size,
      uint8_t is_signed, uint8_t show_plus_sign,
      uint8_t draw_zeros){
      uint8_t width;
      uint8_t required;
      uint8_t pos;
      uint8_t i;
      uint8_t negative;
      uint8_t dot_pos;
      uint32_t scale = 1U;
      uint32_t magnitude;
      uint32_t integer_part;
      uint32_t fractional_part;
      uint32_t divisor;
  
      if (display == 0) { return; }
      width = normalize_range(&start_pos, &stop_pos);
      if (width == 0) { return; }
  
      required = (uint8_t)(int_size + frac_size + (is_signed ? 1U : 0U));
      fill_range(display, start_pos, stop_pos, SPACE_SYM);
  
      /* Remove stale decimal points from the complete destination field. */
      for (i = start_pos; i <= stop_pos; ++i) {
          display->dot_bitmap &= (uint8_t)~(1U << i);
      }
    if ((int_size == 0U) || (required > width) || (required < int_size)) {
          write_error_field(display, start_pos, stop_pos);
          return;
      }
  
      /* Limit the decimal multiplier to a value which fits uint32_t. */
      if (frac_size > 9U) {
          write_error_field(display, start_pos, stop_pos);
          return;
      }
      for (i = 0; i < frac_size; ++i) {
          scale *= 10U;
      }
  
      negative = (value < 0.0f) ? 1U : 0U;
      if (negative) {
          value = -value;
      }
  
      /* Round to the requested number of fractional digits. */
      if (value > (4294967295.0f / (float)scale)) {
          write_error_field(display, start_pos, stop_pos);
          return;
      }
      magnitude = (uint32_t)(value * (float)scale + 0.5f);
      integer_part = magnitude / scale;
      fractional_part = magnitude % scale;
  
      divisor = 1U;
      for (i = 1U; i < int_size; ++i) {
          divisor *= 10U;
      }
      if (integer_part >= (divisor * 10U)) {
          write_error_field(display, start_pos, stop_pos);
          return;
      }
  
      pos = (uint8_t)(stop_pos + 1u - required);
  
      if (is_signed) {
          if (negative) {
              display->display_buffer[pos] = MINIUS_SYM;
          } else if (show_plus_sign) {
              display->display_buffer[pos] = PLUS_SYM;
          } else {
              display->display_buffer[pos] = SPACE_SYM;
          }
          ++pos;
      }
  
      for (i = 0; i < int_size; ++i) {
          uint8_t digit = (uint8_t)(integer_part / divisor);
          integer_part %= divisor;
  
          if ((!draw_zeros) && (digit == 0U) &&
              (i < (uint8_t)(int_size - 1U))) {
              display->display_buffer[pos] = SPACE_SYM;
          } else {
              draw_zeros = 1U;
              display->display_buffer[pos] = digits[digit];
          }
          ++pos;
          if (divisor > 1U) {
              divisor /= 10U;
          }
      }
  
      dot_pos = (uint8_t)(pos - 1U);
      if (frac_size != 0U) {
          display->dot_bitmap |= (uint8_t)(1U << dot_pos);
      } else {
          display->dot_bitmap &= (uint8_t)~(1U << dot_pos);
      }
  
      divisor = scale / 10U;
      for (i = 0; i < frac_size; ++i) {
          uint8_t digit = (uint8_t)(fractional_part / divisor);
          fractional_part %= divisor;
          display->display_buffer[pos++] = digits[digit];
          if (divisor > 1U) {
              divisor /= 10U;
          }
      }
  }


 /** @brief Writes a integer value on LED display
  Takes a integer value, and displays it on the display according 
  to the options.
  

  @param[inout] *display led14seg_display_t Display object
  @param[in] value Signed int16. Value for display
  @param[in] start_pos Unsigned int8. Zero-based first buffer index
  @param[in] stop_pos Unsigned int8. Zero-based last buffer index, inclusive
  @param[in] is_signed Unsigned int8. Reserve one symbol place for sign. 
      If the input value is negative, and this option is 0 - display as positive
  @param[in] show_plus_sign Unsigned int8. If option is 1, display "+" for 
      positive values, else display space symbol
  @param[in] draw_zeros Unsigned int8. Draw leading zeros
  */
  void write_int(led14seg_display_t *display, int16_t value,
      uint8_t start_pos, uint8_t stop_pos,
      uint8_t is_signed, uint8_t show_plus_sign,
      uint8_t draw_zeros){
  
      uint8_t width;
      uint8_t digit_width;
      uint8_t pos;
      uint8_t i;
      uint8_t negative;
      uint32_t magnitude;
      uint32_t divisor = 1U;
  
      if (display == 0) { return; }
      width = normalize_range(&start_pos, &stop_pos);
      if (width == 0U) { return; }
  
      fill_range(display, start_pos, stop_pos, bitmap(' '));
      if (is_signed && (width < 2U)) {
          write_error_field(display, start_pos, stop_pos);
          return;
      }
  
      negative = (value < 0) ? 1U : 0U;
      magnitude = negative ? (uint32_t)(-(int32_t)value) : (uint32_t)value;
      digit_width = (uint8_t)(width - (is_signed ? 1U : 0U));
  
      for (i = 1U; i < digit_width; ++i) {
          divisor *= 10U;
      }
      if (magnitude >= (divisor * 10U)) {
          write_error_field(display, start_pos, stop_pos);
          return;
      }
  
      pos = start_pos;
      if (is_signed) {
          if (negative) {
              display->display_buffer[pos] = MINIUS_SYM;
          } else if (show_plus_sign) {
              display->display_buffer[pos] = PLUS_SYM;
          } else {
              display->display_buffer[pos] = SPACE_SYM;
          }
          ++pos;
      }
  
      for (i = 0; i < digit_width; ++i) {
          uint8_t digit = (uint8_t)(magnitude / divisor);
          magnitude %= divisor;
  
          if ((!draw_zeros) && (digit == 0U) &&
              (i < (uint8_t)(digit_width - 1U))) {
              display->display_buffer[pos] = SPACE_SYM;
          } else {
              draw_zeros = 1U;
              display->display_buffer[pos] = digits[digit];
          }
          ++pos;
          if (divisor > 1U) {
              divisor /= 10U;
          }
      }
  }
