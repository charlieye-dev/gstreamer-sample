SRC = $(wildcard *.c)
TARGET = $(patsubst %.c, %, $(SRC))
all: $(TARGET)

%:%.c
	gcc -o $@ $< `pkg-config --libs --cflags gstreamer-video-1.0 gtk+-3.0 gstreamer-1.0`

clean:
	rm -f $(TARGET)
