server: main.cpp ./timer/lst_timer.h  ./timer/lst_timer.cpp ./http/http_conn.cpp ./http/http_epoll.cpp ./log/log.cpp ./cgi-mysql/sql_connection_pool.cpp webserver.cpp config.h config.cpp
	g++ -g -o server main.cpp ./timer/lst_timer.h  ./timer/lst_timer.cpp ./http/http_conn.cpp ./http/http_epoll.cpp ./log/log.cpp ./cgi-mysql/sql_connection_pool.cpp webserver.cpp config.h config.cpp -lpthread -lmysqlclient

.PHNOY: clean
clean:
	rm -rf server
	rm -rf *-ServerLog
	rm -rf core-server*