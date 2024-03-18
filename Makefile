CFLAGS := -g -O2 -Wall -Wextra -Werror
LDLIBS := -lm

TESTS := testa.dat testb.dat

all: $(TESTS)

$(TESTS): stl2ldraw

%.dat: %.stl
	./stl2ldraw $< $@

testa.stl: test.scad
	openscad --hardwarnings --export-format asciistl -o $@ $^

testb.stl: test.scad
	openscad --hardwarnings --export-format binstl -o $@ $^

clean:
	rm -f stl2ldraw *.o *.stl *.dat
