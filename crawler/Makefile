mycrawler : client.o TrieImplementation.o list.o
	@echo "Compile Crawler...";
	gcc client.o TrieImplementation.o list.o -o mycrawler -pthread

client.o : client.c
	@echo "Compile client...";
	gcc -c client.c -o client.o

TrieImplementation.o : TrieImplementation.c
	@echo "Compile TrieImplementation...";
	gcc -c TrieImplementation.c -o TrieImplementation.o

list.o : list.c
	@echo "Compile list...";
	gcc -c list.c -o list.o

clean: 
	-rm -f *.o
	-rm -f mycrawler
