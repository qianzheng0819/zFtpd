main: *.c
	gcc -Wall -g *.c -lpthread -o server
clean:  server
	rm  server