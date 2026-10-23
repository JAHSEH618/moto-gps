#include "flash_guard.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace moto::device {
namespace {

SemaphoreHandle_t g_flash_mutex = nullptr;

}  // namespace

void flash_guard_init() {
  if (g_flash_mutex == nullptr) {
    g_flash_mutex = xSemaphoreCreateMutex();
  }
}

bool flash_guard_take(std::uint32_t timeout_ms) {
  if (g_flash_mutex == nullptr) return true;  // single-threaded host tests
  return xSemaphoreTake(g_flash_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void flash_guard_give() {
  if (g_flash_mutex != nullptr) xSemaphoreGive(g_flash_mutex);
}

}  // namespace moto::device
