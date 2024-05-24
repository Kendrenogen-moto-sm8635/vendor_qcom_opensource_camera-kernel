# SPDX-License-Identifier: GPL-2.0-only

# Localized KCONFIG settings
CONFIG_SPECTRA_ISP := y
CONFIG_SPECTRA_ICP := y
CONFIG_SPECTRA_JPEG := y
CONFIG_SPECTRA_CRE := y
CONFIG_SPECTRA_SENSOR := y
CONFIG_SPECTRA_LLCC_STALING := y
CONFIG_SPECTRA_USE_RPMH_DRV_API := y
CONFIG_SPECTRA_USE_CLK_CRM_API := y
CONFIG_DOMAIN_ID_SECURE_CAMERA := y
CONFIG_DYNAMIC_FD_PORT_CONFIG := y
CONFIG_CSF_2_5_SECURE_CAMERA := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_SPECTRA_ISP=1
ccflags-y += -DCONFIG_SPECTRA_ICP=1
ccflags-y += -DCONFIG_SPECTRA_JPEG=1
ccflags-y += -DCONFIG_SPECTRA_CRE=1
ccflags-y += -DCONFIG_SPECTRA_SENSOR=1
ccflags-y += -DCONFIG_SPECTRA_LLCC_STALING=1
ccflags-y += -DCONFIG_SPECTRA_USE_RPMH_DRV_API=1
ccflags-y += -DCONFIG_SPECTRA_USE_CLK_CRM_API=1
ccflags-y += -DCONFIG_DOMAIN_ID_SECURE_CAMERA=1
ccflags-y += -DCONFIG_DYNAMIC_FD_PORT_CONFIG=1
ccflags-y += -DCONFIG_CSF_2_5_SECURE_CAMERA=1

# External Dependencies
KBUILD_CPPFLAGS += -DCONFIG_MSM_MMRM=1
ifeq ($(CONFIG_QCOM_VA_MINIDUMP), y)
KBUILD_CPPFLAGS += -DCONFIG_QCOM_VA_MINIDUMP=1
endif
