build:
	gcc -Wall -o rwm rwm.c -lxcb -lxcb-keysyms
	gcc -Wall -o rwmc rwmc.c
	cp -f {rwm,rwmc} ~/bin
