/*
 * Basic led strip example.
 */
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <led_strip.h>

static led_strip_t strip = {
    .gpio = GPIO_NUM_2, // LED data-in pin
    .length = 5,        // Number of LEDs in chain

    // The brightness of all LEDs. If zero when calling led_strip_init(), defaults to max value.
    // Brightness is applied in led_strip_set() and led_strip_fill(). Changing brightness will
    // require calling these methods again.
    .brightness = 0,
};

static void leds_task(void* pvParameter) {
   const rgb_t green = {255, 0, 0};
   const rgb_t red = {0, 255, 0};

   int index = 0;

   ESP_ERROR_CHECK(led_strip_init(&strip)); // allocate required resources

   while (true) {

      ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length - 1, red)); // Set all LEDs to red (indices are clamped to strip length)

      ESP_ERROR_CHECK(led_strip_set(&strip, index, green)); // set LED at index to green (index is clamped to strip length)

      // Note: led_strip_fill() and led_strip_set() only update a buffer. Must use led_strip_flush() to apply changes.
      ESP_ERROR_CHECK(led_strip_flush(&strip)); // update actual LEDs by pushing pixel buffer

      index = (index + 1) % strip.length; // increment index with wrap around

      vTaskDelay(1000 / portTICK_PERIOD_MS);
   }

   ESP_ERROR_CHECK(led_strip_free(&strip)); // free resources, once done
}

void app_main() {
   xTaskCreate(leds_task, "leds_task", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}