target=libthreadpool.so

threadpool_1.0.o:threadpool_1.0.cpp
	g++ $<  -c  


$(target):threadpool_1.0.o
	g++ -fPIC -shared $^ -o $@ -std=c++17

.PHONY:clean
clean:
	-rm $(target) *.o
