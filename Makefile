CC = g++
CFLAGS = -std=c++17 -Isource/include -O2
SRCS = source/omni_core.cpp tools/fs_test.cpp
OUT = tools/fs_test

all: $(OUT)

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $(OUT) $(SRCS)

clean:
	rm -f $(OUT) test_student.omni
