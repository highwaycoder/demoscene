CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
LDLIBS  := -lglfw -lGLEW -lGL -lm
TARGET  := trappedknight

$(TARGET): main.c bg.frag path.vert path.frag cloud.vert cloud.frag
	$(CC) $(CFLAGS) -o $(TARGET) main.c $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) trapped_*.ppm

.PHONY: run clean
