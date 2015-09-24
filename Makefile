obj-m += kcylon.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
install: kcylon.ko
	install -c kcylon.ko /lib/modules/$(shell uname -r)/
	depmod -a
doc:
	doxygen Doxyfile
