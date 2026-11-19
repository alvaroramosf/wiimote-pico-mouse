CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -I../src -Ihost_mocks
LDFLAGS := -lm

SRCS := \
	test_suite.c \
	../src/diamond_tracker.c \
	../src/one_euro_filter.c \
	../src/wiimote_bt.c \
	../src/flash_storage.c \
	../src/ir_leds.c \
	../src/usb_hid.c \
	../src/usb_descriptors.c

TARGET := test_runner

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o
