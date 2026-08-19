CC ?= gcc
CFLAGS += -std=c11 -O2 -Wall -Wextra -Werror
CPPFLAGS += -I../../event
TARGET := person_event_test

all: $(TARGET)

$(TARGET): main.c ../../event/person_event.c ../../event/person_event.h
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c ../../event/person_event.c -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
