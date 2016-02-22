CFLAGS = -g -Wall -I/opt/vc/include -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux/
LDFLAGS = -L/opt/vc/lib
LOADLIBES = -lvoxiUtil -lpng -lGLESv2 -lEGL -lbcm_host -lvcos -pthread -lm

all: piglet

clean:
	rm *.o
	rm piglet minimal

piglet: main.o graphics.o tileTexture.o
	gcc main.o graphics.o tileTexture.o -o piglet $(LDFLAGS) $(LOADLIBES)

main.o: main.c

tileTexture.o: tileTexture.c
