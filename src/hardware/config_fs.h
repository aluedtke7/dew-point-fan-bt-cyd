// Reads config.json off LittleFS and decodes it into a validated Config.
// This is the only place ArduinoJson is used; the validation itself lives in
// the pure, host-tested lib/dewpoint/config.h (parseConfig).
#pragma once

#include "config.h"

namespace dpf {

// Mount LittleFS, read `path`, decode JSON into a RawConfig and validate it.
// On any failure (mount, missing file, bad JSON, invalid values) the returned
// Config has ok == false and error set.
Config loadConfigFromFs(const char* path = "/config.json");

}  // namespace dpf
