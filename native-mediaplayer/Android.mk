LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= native_mediaplayer.cpp

LOCAL_SHARED_LIBRARIES := \
	libstagefright liblog libutils libbinder libui libgui \
	libstagefright_foundation libmedia libcutils libdatasource

LOCAL_C_INCLUDES:= \
	frameworks/av/media/libstagefright \
	frameworks/av/media/libmedia/include \
	frameworks/native/include/media/openmax

LOCAL_CFLAGS += -Wno-multichar -Wall

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= native_mediaplayer

include $(BUILD_EXECUTABLE)
