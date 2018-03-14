CXX = g++
CC = gcc
CXXFLAGS = -O2 -Wall -Werror
CFLAGS = -O2 -Wall -Werror

DEFINES = -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE

RAPIDJSONDIR = /home/cher/rapidjson
RAPIDJSONINCLDIR = $(RAPIDJSONDIR)/include

ALLCXXFLAGS = $(CXXFLAGS) $(DEFINES) -I$(RAPIDJSONINCLDIR) -std=gnu++17
ALLCFLAGS = $(CFLAGS) -std=gnu11 $(DEFINES)

CFILES = \
 base32.c\
 base64.c\
 extract_file.c\
 md5_base64_file.c\
 random.c

CXXFILES = \
 awss3api.cpp\
 subprocess.cpp\
 upload_state.cpp

HFILES = \
 base32.h\
 base64.h\
 extract_file.h\
 md5_base64_file.h\
 random.h

HXXFILES = \
 awss3api.h\
 subprocess.h\
 upload_state.h

OBJECTS = $(CFILES:.c=.o) $(CXXFILES:.cpp=.o)

all : aws-uploader subprocess_test

include deps.make

aws-uploader : aws-uploader.cpp $(OBJECTS)
	$(CXX) $(ALLCXXFLAGS) $^ -o$@ -lcrypto

subprocess_test : subprocess_test.cpp $(OBJECTS)
	$(CXX) $(ALLCXXFLAGS) $^ -o$@ -lcrypto

s3_test : s3_test.cpp $(OBJECTS)
	$(CXX) $(ALLCXXFLAGS) $^ -o$@ -lcrypto

clean :
	-rm -f aws-uploader subprocess_test *.o deps.make

deps.make : $(CFILES) $(HFILES) $(CXXFILES) $(HXXFILES)
	gcc -MM $(CFILES) $(CXXFILES) > deps.make

%.o : %.c
	$(CC) -c $(ALLCFLAGS) $<

%.o : %.cpp
	$(CXX) -c $(ALLCXXFLAGS) $<
