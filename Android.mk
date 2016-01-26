LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := dexposed_art

LOCAL_SRC_FILES := dexposed.cpp

LOCAL_SRC_FILES_arm := art_quick_dexposed_invoke_handler.arm.S
LOCAL_SRC_FILES_arm64 := art_quick_dexposed_invoke_handler.arm64.S
LOCAL_SRC_FILES_x86 := art_quick_dexposed_invoke_handler.x86.S
LOCAL_SRC_FILES_x86_64 := art_quick_dexposed_invode_handler.x86_64.S

LOCAL_CPPFLAGS := -O3 -DNDEBUG

LOCAL_CFLAGS += -std=c++0x -O0 -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION) -Wno-unused-parameter 

LOCAL_C_INCLUDES := \
	$(JNI_H_INCLUDE) \
	art/runtime/ \
	art/runtime/entrypoints/quick/ \
	dalvik/ \
	$(ART_C_INCLUDES) \
	external/gtest/include \
	external/valgrind/main/include \
	external/valgrind/main/ \

include external/libcxx/libcxx.mk

ifeq (1,$(strip $(shell expr $(PLATFORM_SDK_VERSION) \>= 22)))
	ifdef ART_IMT_SIZE
	  LOCAL_CFLAGS += -DIMT_SIZE=$(ART_IMT_SIZE)
	else
	  # Default is 64
	  LOCAL_CFLAGS += -DIMT_SIZE=64
	endif
endif

LOCAL_SHARED_LIBRARIES := libutils liblog libart libc++ libcutils libdl

LOCAL_MODULE_TAGS := optional

LOCAL_MULTILIB := both

include $(BUILD_SHARED_LIBRARY)
