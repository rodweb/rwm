build:
	gcc -o rwm rwm.c -lxcb -lxcb-keysyms
	gcc -o rwmc rwmc.c
	cp -f {rwm,rwmc} ~/bin
