#ifndef EFZ_CONFIG_API_H
#define EFZ_CONFIG_API_H

#define EFZ_CONFIG_SCHEMA_VERSION 1

enum {
    EFZ_SETTING_SWITCH,
    EFZ_SETTING_NUMBER,
    EFZ_SETTING_CHOICE,
    EFZ_SETTING_HOTKEY,
    EFZ_SETTING_TEXT
};

typedef struct EfzConfigSetting {
    const char *section;
    const char *key;
    const char *label;
    const char *description;
    int type;
    int minimum, maximum;
    const char *choices;
    const char *fallback;
} EfzConfigSetting;

typedef struct EfzConfigSchema {
    unsigned version;
    unsigned settingSize;
    const char *iniFile;
    unsigned count;
    const EfzConfigSetting *settings;
} EfzConfigSchema;

#ifdef __cplusplus
#  define EFZ_CONFIG_EXPORT extern "C" __declspec(dllexport)
#else
#  define EFZ_CONFIG_EXPORT __declspec(dllexport)
#endif

#endif
