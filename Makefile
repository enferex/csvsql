APP     = csvsql
CC      = gcc
CFLAGS  = -O0 -g3 -Wall
LDFLAGS = -lsqlite3 -lreadline

all: $(APP)

$(APP): main.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	$(RM) $(APP)
