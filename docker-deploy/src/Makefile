runproxy: runproxy.o proxy.o cache.o
	g++ -std=gnu++11 $^ -o $@ -lpthread

runproxy.o: runproxy.cpp
	g++ -std=gnu++11 -c $< -o $@

proxy.o: proxy.cpp
	g++ -std=gnu++11 -c $< -o $@

cache.o: cache.cpp
	g++ -std=gnu++11 -c $< -o $@

.PHONY: clean
clean:
	rm -rf runproxy *.o proxy.log

cache.cpp: cache.hpp
proxy.cpp: proxy.hpp client.hpp request.hpp response.hpp mytime.hpp