# ThreadPool
利用C++17重构的线程池,更适合于实际应用


关键改造点：
使用可变参模板和引用折叠，实现了线程池提交任务接口支持任意返回值和任意传入参数的任务函数;
使用future类型定制submitTask提交任务的返回值


使用说明：
参考test.cpp
