LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= native_dec_test.cpp

LOCAL_SHARED_LIBRARIES := \
	libstagefright liblog libutils libbinder libstagefright_foundation \
	libmedia libcutils libgui libui

LOCAL_C_INCLUDES:= \
	frameworks/av/media/libstagefright \
	$(TOP)/frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Wno-multichar -Wall

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= native_dec_test

include $(BUILD_EXECUTABLE)

################################################################################

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= native_enc_test.cpp

LOCAL_SHARED_LIBRARIES := \
	libstagefright liblog libutils libbinder libstagefright_foundation \
	libmedia libcutils

LOCAL_C_INCLUDES:= \
	frameworks/av/media/libstagefright \
	$(TOP)/frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Wno-multichar -Wall

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= native_enc_test

include $(BUILD_EXECUTABLE)
