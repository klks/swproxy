COMPILE_TYPE=android

ifeq ($(COMPILE_TYPE),arm)
	CROSS=arm-linux-gnueabi-
	LIBPATH=-L/usr/arm-linux-gnueabi/lib
	EXE=swproxy_arm
else ifeq ($(COMPILE_TYPE),mips)
	CROSS=mipsel-linux-gnu-
	LIBPATH=-L/usr/mipsel16/lib
	EXE=swproxy_mips
	CROSS_CCFLAGS=-static
else ifeq ($(COMPILE_TYPE),android)
	CROSS=arm-linux-androideabi-
	LIBPATH=-L/usr/arm-linux-androideabi/lib
	EXE=swproxy_android
else 
	LIBPATH=-L/usr/lib
	EXE=swproxy_pc
endif

UPX=./upx
UPX_EXE=upx_${EXE}
CC=$(CROSS)gcc
STRIP=$(CROSS)strip
CCFLAGS=$(CROSS_CCFLAGS) -Wall
LD_FLAGS+= -pthread
SRC=swproxy.c
OBJ=${SRC:.c=.o}

all: ${EXE}

${EXE}: ${OBJ}
	$(CC) ${CCFLAGS} ${LIBPATH} ${LD_FLAGS} -o ${EXE} ${OBJ}
	${STRIP} ${EXE}
	${UPX} -9 ${EXE} -o ${UPX_EXE}

%.o: %.c
	${CC} -c $^ ${CCFLAGS} ${LIBPATH} ${LD_FLAGS}

clean:
	rm -f ${OBJ} ${EXE} ${UPX_EXE}
