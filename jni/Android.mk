LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := weasel
LOCAL_SRC_FILES := weasel.c utils.c http_fetcher.c http_error_codes.c
LOCAL_C_INCLUDES := dalvik
LOCAL_STATIC_LIBRARIES := libdex
LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog
include $(BUILD_EXECUTABLE)