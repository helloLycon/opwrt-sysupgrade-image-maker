TARGET=bin-area/a.out
${TARGET}: main.cpp
	g++ $< -o $@

clean:
	rm -rf ${TARGET}
