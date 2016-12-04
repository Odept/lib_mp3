CC = g++
CFLAGS = -std=c++11 -Wall -Wextra -Werror
CFLAGS += -g3

AR = ar
ARFLAGS = rvs

INCLUDES =
# -I/home/newhall/include  -I../include

LFLAGS =
# -L/home/newhall/lib  -L../lib

LIBS = External/lib/mpeg.a External/lib/tag.a
# -lmylib -lm

#SRCS = header.cpp

DEPS = External/inc/mpeg.h External/inc/tag.h
LDEPS = External/lib/mpeg.a External/lib/tag.a
# common.h

# Below we are replacing the suffix .c of all words in the macro SRCS
# with the .o suffix
#OBJS = $(SRCS:.c=.o)

TARGET = mp3
TEST = test

# the first target is executed by default
default: $(TARGET).a

$(TARGET).a: $(TARGET).o $(LDEPS)
	$(AR) $(ARFLAGS) $(TARGET).a $(TARGET).o $(LIBS)
	@echo "###" \"$(TARGET)\" generated

# Object
$(TARGET).o: $(TARGET).cpp $(TARGET).h $(DEPS)
	$(CC) $(CFLAGS) -c $(INCLUDES) $(TARGET).cpp $(LFLAGS)
	@echo "###" \"$(TARGET)\" generated

# Test
test: $(TEST).cpp $(TARGET).a
	$(CC) $(CFLAGS) -o $(TEST) $(TEST).cpp $(TARGET).a
	@echo "###" \"$(TEST)\" generated

clean: 
	$(RM) *.o *~ $(TARGET).a $(TEST)
