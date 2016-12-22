CC = g++
CFLAGS  = -std=c++11 -Wall -Wextra -Werror
CFLAGS += -g3

AR = ar

LIBS = External/lib
LIB_MPEG = $(LIBS)/mpeg.a
LIB_TAG = $(LIBS)/tag.a

DEPS = External/inc/mpeg.h External/inc/tag.h

TARGET = mp3
TEST = test


default: $(TARGET).a

$(TARGET).a: $(TARGET).o $(LIB_MPEG) $(LIB_TAG)
	# Delete an old archive to avoid strange warnings
	rm -f $(TARGET).a
	@echo "# generate" \"$(TARGET)\"
	$(AR) xv $(LIB_MPEG)
	$(AR) xv $(LIB_TAG)
	rm *.SYMDEF
	$(AR) rvs $(TARGET).a *.o

# Object
$(TARGET).o: $(TARGET).cpp $(TARGET).h $(DEPS)
	@echo "# generate" \"$(TARGET)\"
	$(CC) $(CFLAGS) -c $(INCLUDES) $(TARGET).cpp

# Test
test: $(TEST).cpp $(TARGET).a
	@echo "# generate" \"$(TEST)\"
	$(CC) $(CFLAGS) -liconv -o $(TEST) $(TEST).cpp $(TARGET).a

clean: 
	$(RM) *.o *~ $(TARGET).a $(TEST)
	$(RM) -r $(TEST).dSYM
