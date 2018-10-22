all: httpd

httpd: httpd.cpp
	gcc -W -Wall -lpthread -o httpd httpd.cpp

clean:
	rm httpd
