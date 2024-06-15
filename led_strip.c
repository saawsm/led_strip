#include "led_strip.h"

#include <esp_check.h>
#include <esp_log.h>

static const char* TAG = "led_strip";

#define RMT_LED_STRIP_RESOLUTION_HZ (10000000) // 10MHz resolution, 1 tick = 0.1us (tight LED timings, need high resolution)
#define COLOR_COMPONENTS_PER_LED (3)

static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t* ret_encoder);

typedef struct {
   rmt_encoder_t base;
   rmt_encoder_t* bytes_encoder;
   rmt_encoder_t* copy_encoder;
   int state;
   rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

esp_err_t led_strip_init(led_strip_t* led_strip) {
   ESP_RETURN_ON_FALSE(led_strip, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   if (led_strip->brightness == 0)
      led_strip->brightness = 255;

   led_strip->internal.channel = NULL;
   led_strip->internal.encoder = NULL;

   // create pixel buffer
   led_strip->internal.buf = calloc(led_strip->length * COLOR_COMPONENTS_PER_LED, sizeof(uint8_t));

   esp_err_t ret = ESP_OK;
   ESP_GOTO_ON_FALSE(led_strip->internal.buf, ESP_ERR_NO_MEM, err, TAG, "No memory for LED pixel buffer");

   // create RMT tx channel
   rmt_tx_channel_config_t tx_ch_config = {
       .clk_src = RMT_CLK_SRC_DEFAULT,
       .gpio_num = led_strip->gpio,
       .mem_block_symbols = 64,
       .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
       .trans_queue_depth = 4,
   };
   ESP_GOTO_ON_ERROR(rmt_new_tx_channel(&tx_ch_config, &led_strip->internal.channel), err, TAG, "Failed to create RMT TX channel");

   // create RMT LED strip encoder
   ESP_GOTO_ON_ERROR(rmt_new_led_strip_encoder(&led_strip->internal.encoder), err, TAG, "Failed to create LED strip encoder");

   // enable RMT channel
   ESP_GOTO_ON_ERROR(rmt_enable(led_strip->internal.channel), err, TAG, "Failed to enable RMT channel");

   return ESP_OK;

err:
   rmt_disable(led_strip->internal.channel);
   rmt_del_encoder(led_strip->internal.encoder);
   rmt_del_channel(led_strip->internal.channel);
   free(led_strip->internal.buf);
   return ret;
}

esp_err_t led_strip_free(led_strip_t* led_strip) {
   ESP_RETURN_ON_FALSE(led_strip, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   esp_err_t err = ESP_OK;

   // disable rmt channel
   err = rmt_disable(led_strip->internal.channel);
   if (err != ESP_OK)
      goto end;

   // delete rmt encoder
   err = rmt_del_encoder(led_strip->internal.encoder);
   if (err != ESP_OK)
      goto end;

   // delete rmt channel
   err = rmt_del_channel(led_strip->internal.channel);

end:
   free(led_strip->internal.buf);
   return err;
}

esp_err_t led_strip_flush(led_strip_t* led_strip) {
   ESP_RETURN_ON_FALSE(led_strip, ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

   rmt_transmit_config_t tx_config = {.loop_count = 0};

   const uint16_t count = led_strip->length * COLOR_COMPONENTS_PER_LED;

   // start RMT transmit using the pixel buffer
   esp_err_t err = rmt_transmit(led_strip->internal.channel, led_strip->internal.encoder, led_strip->internal.buf, count, &tx_config);
   if (err != ESP_OK)
      return err;

   // wait until buffer has been sent
   return rmt_tx_wait_all_done(led_strip->internal.channel, -1);
}

// Source: esp-idf-lib/lib8tion - scale8.h
/// The "video" version of scale8 guarantees that the output will be only zero if one or both of the inputs are zero.
/// If both inputs are non-zero, the output is guaranteed to be non-zero.
/// This makes for better 'video'/LED dimming, at the cost of several additional cycles.
__attribute__((always_inline)) static inline uint8_t scale8_video(uint8_t i, uint8_t scale) {
   return (((int)i * (int)scale) >> 8) + ((i && scale) ? 1 : 0);
}

__attribute__((always_inline)) static inline void set_pixel(led_strip_t* led_strip, uint16_t index, rgb_t color) {
   const uint16_t pixel_index = index * COLOR_COMPONENTS_PER_LED;
   led_strip->internal.buf[pixel_index + 0] = scale8_video(color.r, led_strip->brightness); // red
   led_strip->internal.buf[pixel_index + 1] = scale8_video(color.g, led_strip->brightness); // green
   led_strip->internal.buf[pixel_index + 2] = scale8_video(color.b, led_strip->brightness); // blue
}

esp_err_t led_strip_set(led_strip_t* led_strip, uint16_t index, rgb_t color) {
   ESP_RETURN_ON_FALSE(led_strip, ESP_ERR_INVALID_ARG, TAG, "Invalid led_strip arg");

   if (index >= led_strip->length)
      index = led_strip->length - 1;

   set_pixel(led_strip, index, color);
   return ESP_OK;
}

esp_err_t led_strip_fill(led_strip_t* led_strip, uint16_t index, uint16_t count, rgb_t color) {
   ESP_RETURN_ON_FALSE(led_strip, ESP_ERR_INVALID_ARG, TAG, "Invalid led_strip arg");

   if (index >= led_strip->length)
      index = led_strip->length - 1;

   uint16_t end = index + count;
   if (end > led_strip->length)
      end = led_strip->length;

   for (uint16_t i = index; i < end; i++)
      set_pixel(led_strip, index, color);

   return ESP_OK;
}

static size_t rmt_encode_led_strip(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primary_data, size_t data_size, rmt_encode_state_t* ret_state) {
   rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
   rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
   rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;

   rmt_encode_state_t session_state = 0;
   rmt_encode_state_t state = 0;

   size_t encoded_symbols = 0;
   switch (led_encoder->state) {
      case 0: // send RGB data
         encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
         if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1; // switch to next state when current encoding session finished
         }
         if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
         }
      // fall-through
      case 1: // send reset code
         encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code, sizeof(led_encoder->reset_code), &session_state);
         if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 0; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
         }
         if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
         }
   }
out:
   *ret_state = state;
   return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t* encoder) {
   rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
   rmt_del_encoder(led_encoder->bytes_encoder);
   rmt_del_encoder(led_encoder->copy_encoder);
   free(led_encoder);
   return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t* encoder) {
   rmt_led_strip_encoder_t* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
   rmt_encoder_reset(led_encoder->bytes_encoder);
   rmt_encoder_reset(led_encoder->copy_encoder);
   led_encoder->state = 0;
   return ESP_OK;
}

static esp_err_t rmt_new_led_strip_encoder(rmt_encoder_handle_t* ret_encoder) {
   rmt_led_strip_encoder_t* led_encoder = calloc(1, sizeof(rmt_led_strip_encoder_t));

   esp_err_t ret = ESP_OK;
   ESP_GOTO_ON_FALSE(led_encoder, ESP_ERR_NO_MEM, err, TAG, "No memory for LED strip encoder");

   led_encoder->base.encode = rmt_encode_led_strip;
   led_encoder->base.del = rmt_del_led_strip_encoder;
   led_encoder->base.reset = rmt_led_strip_encoder_reset;

   // LED timings for WS2812
   rmt_bytes_encoder_config_t bytes_encoder_config = {
       .bit0 =
           {
               .level0 = 1,
               .duration0 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0H=0.3us
               .level1 = 0,
               .duration1 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T0L=0.9us
           },
       .bit1 =
           {
               .level0 = 1,
               .duration0 = 0.9 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1H=0.9us
               .level1 = 0,
               .duration1 = 0.3 * RMT_LED_STRIP_RESOLUTION_HZ / 1000000, // T1L=0.3us
           },
       .flags.msb_first = 1                                              // WS2812 transfer bit order: G7...G0 R7...R0 B7...B0
   };

   ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder), err, TAG, "Create bytes encoder failed");

   rmt_copy_encoder_config_t copy_encoder_config = {};
   ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder), err, TAG, "Create copy encoder failed");

   uint32_t reset_ticks = RMT_LED_STRIP_RESOLUTION_HZ / 1000000 * 50 / 2; // reset code duration defaults to 50us
   led_encoder->reset_code = (rmt_symbol_word_t){
       .level0 = 0,
       .duration0 = reset_ticks,
       .level1 = 0,
       .duration1 = reset_ticks,
   };

   *ret_encoder = &led_encoder->base;
   return ESP_OK;

err:
   if (led_encoder) {
      if (led_encoder->bytes_encoder)
         rmt_del_encoder(led_encoder->bytes_encoder);

      if (led_encoder->copy_encoder)
         rmt_del_encoder(led_encoder->copy_encoder);

      free(led_encoder);
   }
   return ret;
}