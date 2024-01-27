CC = gcc
CFLAGS = -g -Wall -I /home/pn-cs357/Given/Mush/libmush/include
LDFLAGS = -L /home/pn-cs357/Given/Mush/libmush/lib64/
TARGET = mush2

SRC = mush2.c

all: $(TARGET)

$(TARGET): $(SRC)
	${CC} ${CFLAGS} -o ${TARGET} ${SRC} ${LDFLAGS} -lmush

clean:
	rm -f *.o $(TARGET)
