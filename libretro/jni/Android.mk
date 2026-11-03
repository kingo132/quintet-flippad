LOCAL_PATH := $(call my-dir)

CORE_DIR := $(LOCAL_PATH)/../..

include $(CORE_DIR)/libretro/Makefile.common

COREFLAGS := -DANDROID -D__LIBRETRO__ $(INCFLAGS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(SOURCES_C) $(SOURCES_CXX)
LOCAL_CXXFLAGS  := $(COREFLAGS) -std=c++17 -O3 -fno-strict-aliasing
LOCAL_CFLAGS    := $(COREFLAGS) -O3 -fno-strict-aliasing
LOCAL_LDFLAGS   := -Wl,-version-script=$(CORE_DIR)/libretro/link.T
include $(BUILD_SHARED_LIBRARY)
