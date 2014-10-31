
SRC = src/ovs-driver.c
LIB = lib/
OBJ = $(SRC:.c=.o)
OUT = libovsdriver.a
INCLUDES = -I. -Ithird-party/ovs/include -Ithird-party/ovs
CCFLAGS = -g -w
CC = gcc
LDFLAGS = -g

.SUFFIXES: .c

default: $(OUT)

.c.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

$(OUT): $(OBJ)
	ar rcs $(OUT) $(OBJ)
	mv $(OUT) $(LIB)

clean:
	rm -f $(OBJ) $(OUT)
