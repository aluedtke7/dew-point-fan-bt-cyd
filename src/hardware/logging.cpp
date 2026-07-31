#include "logging.h"

#include <Arduino.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "format.h"
#include "log_buffer.h"

namespace dpf {

namespace {
LogBuffer g_log(50);                   // capacity 50 (matches /logs)
SemaphoreHandle_t g_mutex = nullptr;
}  // namespace

void loggingBegin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
}

void logf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  // Wall-clock prefix, "dd.MM.yy HH:mm:ss " (all zeros before NTP sync), plus
  // heap metrics on every line so a leak/fragmentation trend is visible in
  // /logs: free now, lifetime low watermark, largest allocatable block.
  // Cheap and safe to query from any FreeRTOS task.
  char ts[24];
  formatLogTimestamp(ts, sizeof(ts), time(nullptr));
  char heap[48];
  formatHeapSuffix(heap, sizeof(heap), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                   heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // Mirror to the serial console, timestamp first (unlocked, status quo).
  Serial.print(ts);
  Serial.print(' ');
  Serial.print(heap);
  Serial.print(' ');
  Serial.print(buf);

  if (g_mutex == nullptr) return;  // logged before loggingBegin(): Serial only

  // Store the message without its trailing newline so /logs shows one line each.
  size_t len = (n < 0) ? 0 : (n < static_cast<int>(sizeof(buf)) ? n : sizeof(buf) - 1);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) --len;

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  // Build the full log line in a stack buffer: "ts heap message"
  char line[sizeof(ts) + sizeof(heap) + sizeof(buf) + 3];
  snprintf(line, sizeof(line), "%s %s %.*s", ts, heap, static_cast<int>(len),
           buf);
  g_log.push(line);
  xSemaphoreGive(g_mutex);
}

void streamLogs(WebServer& server) {
  if (g_mutex == nullptr) return;

  xSemaphoreTake(g_mutex, portMAX_DELAY);

  const size_t n = g_log.size();
  // Compute total content length: each line + '\n'.
  size_t total = 0;
  for (size_t i = 0; i < n; ++i) {
    total += std::strlen(g_log.lineAt(i)) + 1;
  }

  server.setContentLength(total);
  server.send(200, "text/plain", "");
  for (size_t i = 0; i < n; ++i) {
    server.sendContent(g_log.lineAt(i));
    server.sendContent("\n");
  }

  xSemaphoreGive(g_mutex);
}

}  // namespace dpf
