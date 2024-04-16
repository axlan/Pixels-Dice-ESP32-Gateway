#pragma once

#include <vector>

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

struct ParamEntry
{
    static constexpr size_t DEFAULT_FIELD_SIZE = 40;
    const char *label = "";
    const char *default_val = "";
    int max_len = 0;
    const char *id = "";
    // NOTE: id must not contain spaces or special HTML characters.
    constexpr ParamEntry(const char *label,
                         const char *default_val = "",
                         size_t max_len = DEFAULT_FIELD_SIZE,
                         const char *id = nullptr) : label(label),
                                                     default_val(default_val),
                                                     max_len(max_len),
                                                     id((id == nullptr) ? label : id) {}
};

class WifiManagerParamHelper
{
public:
    WifiManagerParamHelper(WiFiManager &wm) : wm_(wm) {}

    void Init(uint16_t preamble, const ParamEntry *entries, size_t entries_len, bool skip_load = false);

    const char *GetSettingValue(size_t idx) const;

    const char *GetSettingValue(const char *id_ptr) const;

    const WiFiManagerParameter &GetSettingParam(size_t idx) const;

    const WiFiManagerParameter &GetSettingParam(const char *id_ptr) const;

    size_t GetNumSettings() const;

    static bool str2int(const char* val, int* out);

private:
    static constexpr size_t HEADER_SIZE = sizeof(uint16_t) * 2;
    size_t _data_size = 0;
    WiFiManager &wm_;
    std::vector<WiFiManagerParameter> parameters_;

    void OnParamCallback();
};
