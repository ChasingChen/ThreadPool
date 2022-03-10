// 线程池项目.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <chrono>
#include <thread>
using namespace std;

#include "threadpool_1.0.h"

/*
有些场景，是希望能够获取线程执行任务得返回值得
举例：
1 + 。。。 + 30000的和
thread1  1 + ... + 10000
thread2  10001 + ... + 20000
.....

main thread：给每一个线程分配计算的区间，并等待他们算完返回结果，合并最终的结果即可
*/

using uLong = unsigned long long;

class MyTask : public Task
{ 
public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end)
    {}   
    // 怎么设计run函数的返回值，可以表示任意的类型
    // 这里仿照C++17 Any类型实现  为啥不用模板，因为模板函数和虚函数是不能写在一块的
    Any run()  // run方法最终就在线程池分配的线程中去做执行了!
    {
        std::cout << "tid:" << std::this_thread::get_id()
            << "begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++)
            sum += i;
        std::cout << "tid:" << std::this_thread::get_id()
            << "end!" << std::endl;

        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{
    {   //括一个作用域 ThreadPool pool会自动回收
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start(2);

        // linux上，这些Result对象也是局部对象，要析构的！！！
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));

        //uLong sum1 = res1.get().cast_<uLong>();
        //cout << sum1 << endl; 
    } // 这里Result对象也要析构!!! 在vs下，条件变量析构会释放相应资源的
    
    cout << "main over!" << endl;
    getchar();
#if 0
    // ThreadPool对象析构以后，怎么样把线程池相关的线程资源全部回收？
    {
        ThreadPool pool;
        // 用户自己设置线程池的工作模式
        pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start(4);

        // 设计Result使得其能获取各线程任意未来类型的运算结果
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        // 随着task被执行完，task对象没了，依赖于task对象的Result对象也没了
        uLong sum1 = res1.get().cast_<uLong>();  // get返回了一个Any类型，需要转成具体类型
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();

        // Master - Slave线程模型
        // Master线程用来分解任务，然后给各个Slave线程分配任务
        // 等待各个Slave线程执行完任务，返回结果
        // Master线程合并各个任务结果，输出
        cout << (sum1 + sum2 + sum3) << endl;
    }
    


    /*uLong sum = 0;
    for (uLong i = 1; i <= 300000000; i++)
        sum += i;
    cout << sum << endl;*/

    /*pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());*/
 
    getchar();

#endif
}
