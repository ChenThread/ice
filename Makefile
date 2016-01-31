LIBS = -lm
INCLUDES = argparse/argparse.h
OBJS = argparse/argparse.o ice.o

build/%.o: %.c $(INCLUDES)
	$(CC) -c -o $@ $< $(CFLAGS)

ice: $(OBJS)
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f $(OBJS) ice
