SDK_DIR    := $(HOME)/duos-sdk
CC         := aarch64-linux-gnu-gcc

CVITEK_INC := $(SDK_DIR)/cvi_mpi/include
CVITEK_LIB := $(SDK_DIR)/cvi_mpi/lib

JPEG_INC   := $(HOME)/jpeg-arm64/include
JPEG_LIB   := $(HOME)/jpeg-arm64/lib

CFLAGS := -O2 -Wall \
          -I$(CVITEK_INC) \
          -I$(CVITEK_INC)/linux \
          -I$(JPEG_INC) \
          -march=armv8-a

LDFLAGS := -static -L$(CVITEK_LIB) \
           -L$(JPEG_LIB) \
           -lvenc -lsys \
           -ljpeg -lpthread \
           -Wl,-rpath-link,$(CVITEK_LIB) \
           -lm

TARGET := camera_daemon

all: $(TARGET)

$(TARGET): camera_daemon.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build OK: $@"

clean:
	rm -f $(TARGET)

.PHONY: all clean