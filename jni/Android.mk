LOCAL_PATH := $(call my-dir)

MODLOADER := ../../AoQ-ModLoader-For-Quest

include $(CLEAR_VARS)
LOCAL_MODULE    := levigrip
LOCAL_LDLIBS    := -llog
LOCAL_SRC_FILES := \
    ../src/main.c \
    $(MODLOADER)/shared/aoqcore/aoq.c \
    $(MODLOADER)/shared/inline-hook/inlineHook.c \
    $(MODLOADER)/shared/inline-hook/relocate.c \
    $(MODLOADER)/shared/utils/utils.c \
    $(MODLOADER)/modmanager/modconfig.c \
    $(MODLOADER)/modmanager/cJSON.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(MODLOADER)/modmanager
include $(BUILD_SHARED_LIBRARY)
