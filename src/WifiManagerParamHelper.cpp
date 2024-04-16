#include "WifiManagerParamHelper.h"

#include <cstdlib>

#include <EEPROM.h>

static const WiFiManagerParameter DUMMY_PARAM = WiFiManagerParameter();
 
void WifiManagerParamHelper::Init(uint16_t preamble, const ParamEntry* entries, size_t entries_len, bool skip_load) {
  uint16_t read_buffer = 0;

  _data_size = 0;
  for (size_t i = 0; i < entries_len; i++) {
    _data_size += entries[i].max_len;
  }

  // Could predetermine, actual size, but this is simpler.
  // This logic should only dirty the EEPROM when new parameters are added or
  // fist use.
  EEPROM.begin(HEADER_SIZE + _data_size);

  // Presize to avoid needing to relocate members.
  parameters_.clear();
  parameters_.reserve(entries_len);

  wm_.setSaveParamsCallback(
      std::bind(&WifiManagerParamHelper::OnParamCallback, this));

  bool valid = !skip_load && EEPROM.get(0, read_buffer) == preamble;
  uint16_t eeprom_length = (valid) ? EEPROM.get(2, read_buffer) : 0;

  uint16_t current_size = HEADER_SIZE;

  for (size_t i = 0; i < entries_len; i++) {
    if (eeprom_length >= current_size + entries[i].max_len) {
      const char *loaded_data = reinterpret_cast<const char *>(
          EEPROM.getDataPtr() + current_size);
      parameters_.emplace_back(entries[i].id, entries[i].label, loaded_data,
                                entries[i].max_len);
      Serial.println(String("Loading: ") + entries[i].id + ": " +
                      loaded_data);
    } else {
      EEPROM.write(current_size, 0);
      parameters_.emplace_back(entries[i].id, entries[i].label, entries[i].default_val,
                                entries[i].max_len);
      Serial.println(String("Creating: ") + entries[i].id);
      strncpy(reinterpret_cast<char *>(EEPROM.getDataPtr() + current_size),
              entries[i].default_val, entries[i].max_len);
    }
    current_size += entries[i].max_len;
    wm_.addParameter(&parameters_.back());
  }
  EEPROM.put(2, current_size);
  EEPROM.put(0, preamble);
  // This is smart enough to only write if the values have been modified.
  EEPROM.end();
}

const char* WifiManagerParamHelper::GetSettingValue(size_t idx) const {
  if (idx < parameters_.size()) {
    return parameters_[idx].getValue();
  }
  return nullptr;
}

const char* WifiManagerParamHelper::GetSettingValue(const char* id_ptr) const {
    for (const auto& param : parameters_) {
        if (param.getID() == id_ptr) {
            return param.getValue();
        }
    }
    return nullptr;
}


const WiFiManagerParameter& WifiManagerParamHelper::GetSettingParam(size_t idx) const {
  if (idx < parameters_.size()) {
    return parameters_[idx];
  }
  return DUMMY_PARAM;
}

const WiFiManagerParameter &WifiManagerParamHelper::GetSettingParam(const char* id_ptr) const {
    for (const auto& param : parameters_) {
        if (param.getID() == id_ptr) {
            return param;
        }
    }
    return DUMMY_PARAM;
}

size_t WifiManagerParamHelper::GetNumSettings() const {
  return parameters_.size();
}

bool WifiManagerParamHelper::str2int(const char* val, int* out) {
    char *end;
    errno = 0;
    *out = std::strtol(val, &end, 10);
    if (*val == '\0' || *end != '\0') {
        return false;
    }
    return true;
}

void WifiManagerParamHelper::OnParamCallback() {
  EEPROM.begin(HEADER_SIZE + _data_size);
  uint16_t current_size = HEADER_SIZE;
  for (const auto &param : parameters_) {
    // Do this check to avoid dirtying EEPROM buffer if nothing changed.
    if (strncmp(reinterpret_cast<const char *>(EEPROM.getDataPtr() +
                                                current_size),
                param.getValue(), param.getValueLength()) != 0) {
      strncpy(reinterpret_cast<char *>(EEPROM.getDataPtr() + current_size),
              param.getValue(), param.getValueLength());
      Serial.println(String("Updating: ") + param.getID());
    }
    current_size += param.getValueLength();
  }
  EEPROM.end();
}
