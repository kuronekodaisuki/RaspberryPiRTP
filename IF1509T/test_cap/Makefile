CFLAGS+=-ftree-vectorize -pipe -Wno-psabi

CFLAGS+=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM

CFLAGS+=-I./ilclient -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux

LDFLAGS+=-L/opt/vc/lib -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lm

OBJS=udpsend.o testcap_main.o rtpavcsend.o omxcam/debug.o omxcam/video.o omxcam/h264.o omxcam/core.o omxcam/error.o omxcam/still.o omxcam/camera.o omxcam/dump_omx.o omxcam/version.o omxcam/event.o omxcam/jpeg.o omxcam/utils.o

TARGET=test_cap_udp
TARGET2=test_cap_rtp

all:: $(TARGET2)

clean:: 
	@rm -f $(TARGET2)

$(TARGET2): $(TARGET)
	@rm -f $@
	cp -p $< $@

include ../makefile.common
