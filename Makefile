LIBS = -lm
INCLUDES = argparse/argparse.h
OBJS = argparse/argparse.o ice.o

all: ice

build/%.o: %.c $(INCLUDES)
	$(CC) -c -o $@ $< $(CFLAGS)

ice: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(OBJS) ice
