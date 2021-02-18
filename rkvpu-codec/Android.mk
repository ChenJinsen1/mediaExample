LOCAL_PATH:= $(call my-dir)

#
# SECTION 1: build test for rkvpu-codec decoder
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	rkvpu_dec_api.cpp \
	rkvpu_dec_test.cpp

LOCAL_SHARED_LIBRARIES := \
	liblog libvpu

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/inc

ifeq (1, $(strip $(shell expr $(PLATFORM_SDK_VERSION) \>= 29)))
LOCAL_C_INCLUDES += \
	$(TOP)/system/core/libutils/include
else
endif

LOCAL_PROPRIETARY_MODULE := true

LOCAL_MULTILIB := 32
LOCAL_MODULE := rkvpu_dec_test
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

#
# SECTION 2: build test for rkvpu-codec encoder
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	rkvpu_enc_api.cpp \
	rkvpu_enc_test.cpp

LOCAL_SHARED_LIBRARIES := \
	liblog libvpu

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/inc

ifeq (1, $(strip $(shell expr $(PLATFORM_SDK_VERSION) \>= 29)))
LOCAL_C_INCLUDES += \
	$(TOP)/system/core/libutils/include
else
endif

LOCAL_PROPRIETARY_MODULE := true

LOCAL_MULTILIB := 32
LOCAL_MODULE := rkvpu_enc_test
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
