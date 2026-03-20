CC := aarch64-unknown-linux-gnu-gcc

JPEG_INC := $(HOME)/jpeg-arm64/include

CFLAGS := -O2 -Wall \
          -march=armv8-a

LDFLAGS := -static \
           -lpthread \
           -lm

TARGET := camera_daemon

all: $(TARGET)

$(TARGET): camera_daemon.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build OK: $@"

clean:
	rm -f $(TARGET)

.PHONY: all clean
