PROG = cache_sim
CC = g++ -g
SRCS = utils.cpp store.cpp memmap.cpp tcache.cpp cache_sim.cpp
OBJS = ${SRCS:.cpp=.o}
CFLAGS+=-DLOG #-DREGRESS

.SUFFIXES: .o .cpp

.cpp.o :
	$(CC) $(CFLAGS) -c $? -o $@

all : $(PROG)

$(PROG) : $(OBJS)
	$(CC) $^ -o $@ -lm

clean :
	rm -rf $(PROG) *.o
