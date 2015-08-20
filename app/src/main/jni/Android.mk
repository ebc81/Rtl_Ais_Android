LOCAL_PATH:= $(call my-dir)
APP_PLATFORM:= android-16
APP_ABI:= armeabi armeabi-v7a x86

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
 libusb-andro/libusb/core.c \
 libusb-andro/libusb/descriptor.c \
 libusb-andro/libusb/hotplug.c \
 libusb-andro/libusb/io.c \
 libusb-andro/libusb/sync.c \
 libusb-andro/libusb/strerror.c \
 libusb-andro/libusb/os/linux_usbfs.c \
 libusb-andro/libusb/os/poll_posix.c \
 libusb-andro/libusb/os/threads_posix.c \
 libusb-andro/libusb/os/linux_netlink.c \
 RtlAisJava.c \
 rtl_ais_andro.c \
 rtl-sdr/src/convenience/convenience.c \
 librtlsdr_andro.c \
 rtl-sdr/src/tuner_e4k.c \
 rtl-sdr/src/tuner_fc0012.c \
 rtl-sdr/src/tuner_fc0013.c \
 rtl-sdr/src/tuner_fc2580.c \
 rtl-sdr/src/tuner_r82xx.c \
 aisdecoder/aisdecoder.c \
 aisdecoder/sounddecoder.c \
 aisdecoder/lib/hmalloc.c  \
 aisdecoder/lib/protodec.c \
 aisdecoder/lib/receiver.c \
 aisdecoder/lib/filter.c
 

LOCAL_C_INCLUDES += \
jni/libusb-andro \
jni/libusb-andro/libusb \
jni/libusb-andro/libusb/os \
jni/rtl-sdr/include \
jni/rtl-sdr/src \
jni/aisdecoder \
jni/aisdecoder/lib 

LOCAL_CFLAGS += -Wall -DLIBUSB_DESCRIBE="" -O3 -fno-builtin-printf -fno-builtin-fprintf
LOCAL_MODULE:= RtlAisJava
LOCAL_LDLIBS := -lm -llog
include $(BUILD_SHARED_LIBRARY)
