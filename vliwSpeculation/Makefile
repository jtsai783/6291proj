CC=/usr/bin/g++
CFLAGS=
OUT=vliwSpeculation

OBJS=vliwSpeculation.o

all:$(OUT)

clean:
	rm -r *.o $(OUT)

$(OUT):$(OBJS)
	$(CC) -o $(OUT) $(OBJS)

%.o:%.cpp
	$(CC) $(CFLAGS) -g -c $< -o $@

vliwSpeculation.o: vliwSpeculation.cpp
