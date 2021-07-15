#
_XDCBUILDCOUNT = 
ifneq (,$(findstring path,$(_USEXDCENV_)))
override XDCPATH = C:/ti/simplelink_cc2640r2_sdk_4_30_00_08/source;C:/ti/simplelink_cc2640r2_sdk_4_30_00_08/kernel/tirtos/packages;C:/ti/simplelink_cc2640r2_sdk_4_30_00_08/source/ti/blestack
override XDCROOT = C:/ti/xdctools_3_51_03_28_core
override XDCBUILDCFG = ./config.bld
endif
ifneq (,$(findstring args,$(_USEXDCENV_)))
override XDCARGS = 
override XDCTARGETS = 
endif
#
ifeq (0,1)
PKGPATH = C:/ti/simplelink_cc2640r2_sdk_4_30_00_08/source;C:/ti/simplelink_cc2640r2_sdk_4_30_00_08/kernel/tirtos/packages;C:/ti/simplelink_cc2640r2_sdk_4_30_00_08/source/ti/blestack;C:/ti/xdctools_3_51_03_28_core/packages;..
HOSTOS = Windows
endif
