build:
	gcc -g -o rwm rwm.c -lxcb -lxcb-keysyms
	gcc -g -o rwmc rwmc.c
	cp -f {rwm,rwmc} ~/bin
