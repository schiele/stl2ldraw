CFLAGS := -g -O2 -Wall -Wextra -Werror
LDLIBS := -lm

test.dat: stl2ldraw test.stl
	./$^ $@

test.stl: test.scad
	openscad --hardwarnings --export-format binstl -o $@ $^

clean:
	rm -f stl2ldraw stl2ldraw.o test.stl test.dat
