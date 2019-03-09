myhttpd : webserver.o server_tools.o
	@echo "Compile Project3...";
	gcc webserver.o server_tools.o -o myhttpd -pthread

webserver.o : webserver.c
	@echo "Compile webserver...";
	gcc -c webserver.c -o webserver.o

server_tools.o : server_tools.c
	@echo "Compile server_tools...";
	gcc -c server_tools.c -o server_tools.o

clean: 
	-rm -f *.o
	-rm -f myhttpd
