#ifndef _LED_STRIP_H
#define _LED_STRIP_H

#include <esp_err.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
   uint8_t r; // red
   uint8_t g; // green
   uint8_t b; // blue
} rgb_t;

typedef struct {
   gpio_num_t gpio;    // The GPIO pin of the data-in of the LED strip
   uint16_t length;    // The number of LEDs in the LED strip
   uint8_t brightness; // Brightness of LED strip. Set zero to turn off all LEDs.

   struct {
      rmt_channel_handle_t channel;
      rmt_encoder_handle_t encoder;

      uint8_t* buf; // pixel buffer. ordered: g,r,b|g,r,b|g,r,b
   } internal;
} led_strip_t;

/**
 * @brief Init LED strip
 *
 * Setup RMT and create pixel buffer.
 *
 * If LED strip brightness is zero, it will be set to max. Can be set to zero after init.
 *
 * @param led_strip Pointer to the LED strip
 * @return ESP_OK on success
 */
esp_err_t led_strip_init(led_strip_t* led_strip);

/**
 * @brief Cleanup LED strip
 *
 * Releases RMT and pixel buffer.
 *
 * @param led_strip Pointer to the LED strip
 * @return ESP_OK on success
 */
esp_err_t led_strip_free(led_strip_t* led_strip);

/**
 * @brief Write buffer into LED strip
 *
 * Starts RMT to update LEDs with current pixel buffer colors.
 *
 * @param led_strip Pointer to the LED strip
 * @return ESP_OK on success
 */
esp_err_t led_strip_flush(led_strip_t* led_strip);

/**
 * @brief Set a single LED to the specified color
 *
 * Sets a single LED (in pixel buffer) to the specified color.
 * Must call led_strip_flush() to update actual LED strip.
 *
 * @param led_strip Pointer to the LED strip
 * @param index The LED index (zero based). Bounds protected
 * @param color The new LED color
 * @return ESP_OK on success
 */
esp_err_t led_strip_set(led_strip_t* led_strip, uint16_t index, rgb_t color);

/**
 * @brief Sets one or more LEDs to the specified color
 *
 * Sets a single LED (in pixel buffer) to the specified color.
 * Must call led_strip_flush() to update actual LED strip.
 *
 * @param led_strip Pointer to the LED strip
 * @param index The starting LED index (zero base). Bounds protected
 * @param count The number of LEDs to set, from index. Bounds protected
 * @param color The new LED color
 * @return ESP_OK on success
 */
esp_err_t led_strip_fill(led_strip_t* led_strip, uint16_t index, uint16_t count, rgb_t color);

#ifdef __cplusplus
}
#endif

#endif // _LED_STRIP_H