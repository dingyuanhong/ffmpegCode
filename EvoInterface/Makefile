CC = gcc
CXX = g++
AR = ar
RM = rm -rf

#原始目录
SRC_PATH :=.
#目标名
TARGET :=EvoInterface
#源文件
SRCS += $(wildcard $(SRC_PATH)/*.cpp)
#中间文件
OBJS := $(SRCS:.cpp=.o)

#动态库
LIBS := stdc++ pthread  \
avcodec avdevice avfilter avformat avutil postproc swresample swscale

#模块库文件
MOULE_LIBRARY_PATH = /usr/lib/ /usr/local/lib/

#头文件路径
INCLUDE_PATH :=. 
#动态库路径
LIBRARY_PATH :=

INCLUDE_PATH += /usr/include
LIBRARY_PATH += $(MOULE_LIBRARY_PATH)


RELEASE = 1
BITS =

#ifeq ( 1 , ${DBG_ENABLE} )
#	CFLAGS += -D_DEBUG -O0 -g -DDEBUG=1
#endif

CFLAGS = -Wall -std=c++11
LFLAGS =

#头文件
CFLAGS += $(foreach dir,$(INCLUDE_PATH),-I$(dir))

#库路径
LDFLAGS += $(foreach lib,$(LIBRARY_PATH),-L$(lib))

#库名
LDFLAGS += $(foreach lib,$(LIBS),-l$(lib))

#检查版本
ifeq ($(RELEASE),0)
	#debug
	CFLAGS += -g
else
	#release
	CFLAGS += -O3 -DNDEBUG
endif

#检查位宽
ifeq ($(BITS),32)
	CFLAGS += -m32
	LFLAGS += -m32
else
	ifeq ($(BITS),64)
		CFLAGS += -m64
		LFLAGS += -m64
	else
	endif
endif


#操作命令
all:clean packet

$(OBJS):%.o:%.cpp
	$(CXX) $(CFLAGS) -c $^ -o $@

build:$(OBJS)
	$(CXX) $(LFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)
	$(RM) $(OBJS)

packet:$(OBJS)
	$(AR) -r lib$(TARGET).a $(OBJS)
	$(RM) $(OBJS)

dynamic:
	$(CXX) -fPIC -shared $(SRCS) $(CFLAGS) $(LDFLAGS) $(LFLAGS) -o $(TARGET).so
	$(RM) $(OBJS)

clean:
	echo $(SRCS)
	$(RM) $(OBJS) $(TARGET)	lib$(TARGET).a $(TARGET).so
