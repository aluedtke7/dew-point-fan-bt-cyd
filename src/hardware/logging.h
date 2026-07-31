// Serial logging with a recent-message ring buffer.
//
// logf() mirrors each message to the serial console exactly as the old raw
// Serial.print* calls did, and also keeps the most recent lines in a LogBuffer
// so they can be served over WiFi at GET /logs. Logs arrive from several
// FreeRTOS tasks on both cores, so the buffer is guarded by a dedicated mutex
// (not App::mutex, which does not exist until app.begin()).
#pragma once

#include <WebServer.h>

namespace dpf {

// Create the log mutex. Call once at the very top of setup(), before the first
// logf(). Logs emitted before this simply skip the buffer (still hit Serial).
void loggingBegin();

// Format like printf, write to Serial verbatim, and append to the ring buffer
// (trailing newline stripped). Include a trailing "\n" for a console line break.
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Stream the recent log lines directly to an HTTP response under the log mutex.
// Avoids copying the entire buffer into a vector.  Computes the total content
// length, sends the header, then streams each line with sendContent().
void streamLogs(WebServer& server);

}  // namespace dpf
