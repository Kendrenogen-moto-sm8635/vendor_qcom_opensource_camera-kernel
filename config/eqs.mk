# Settings for compiling waipio camera architecture

# Localized KCONFIG settings
CONFIG_CAM_SENSOR_PROBE_DEBUG := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_CAM_SENSOR_PROBE_DEBUG=1
