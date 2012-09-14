CC = gcc
CFLAGS = -O2 -D_BSD_SOURCE -D_XOPEN_SOURCE  -D_SVID_SOURCE #-DDEBUG
LDFLAGS = -lXext -lacpi

all:
	$(CC) $(CFLAGS) -c `xosd-config --cflags` -o xbattbar-acpi.o xbattbar-acpi.c
	$(CC) $(LDFLAGS) `xosd-config --libs` -o xbattbar-acpi xbattbar-acpi.o
clean:
	rm -rf xbattbar-acpi
	rm -rf xbattbar-acpi.o

install:
	cp xbattbar-acpi $(DESTDIR)/usr/bin/
	cp xbattbar-acpi.1 $(DESTDIR)/usr/share/man/man1

uninstall:
	rm -rf $(DESTDIR)/usr/bin/xbattbar-acpi
	rm -rf $(DESTDIR)/usr/share/man/man1/xbattbar-acpi.1
