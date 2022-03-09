 #ifndef THREADPOOL_H
#define THREADPOOL_H
//命名规范：变量用驼峰式，下滑线放后面（因为C++库里很多都是把下滑线放前面的）
//函数首字母大写
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>

// Any类型：可以接收任意数据的类型
class Any    //模板的代码都写在一个文件中
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;   //为啥要=delete,unique_ptr和auto_ptr不同之处在于，unique_ptr永远只有一个智能指针指向资源，它把左值引用拷贝构造赋值给禁用掉了，公布了右值引用拷贝构造
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// 这个构造函数可以让Any类型接收任意其它的数据
	template<typename T>  // T:int    Derive<int>
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	// 这个方法能把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 我们怎么从base_找到它所指向的Derive对象，从它里面取出data成员变量
		// 基类指针 =》 派生类指针   RTTI（Run-Time Type Identification):四种强转只有dynamic_cast通过运行时类型信息程序能够使用基类的指针或引用来检查这些指针或引用所指的对象的实际派生类型
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());  //智能指针get()获取裸指针
		if (pd == nullptr)   //避免Any构造时是用int实例化对象，基类指针指向Derive<int>,而强转试图转为long就要提示不成功
		{
			throw "type is unmatch!";
		}
		return pd->data_;  //把派生类的成员变量返回
	}
private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default;  //可以让编译器优化，{}=default用默认实现 
	};

	// 派生类类型
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) 
		{}
		T data_;  // 保存了任意的其它类型
	};

private:
	// 定义一个基类的指针
	std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore
{
public:
	Semaphore(int limit = 0) 
		:resLimit_(limit)
		,isExit_(false);
	{}
	~Semaphore() 
	{
		isExit_=true;	
	}

	// 获取一个信号量资源
	void wait()
	{
		if(isExit_)
		return;
		std::unique_lock<std::mutex> lock(mtx_);
		// 等待信号量有资源，没有资源的话，会阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// 增加一个信号量资源
	void post()
	{
		if(isExit_)
		return;
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// linux下condition_variable的析构函数什么也没做
		// 导致这里状态已经失效，无故阻塞
		cond_.notify_all();  // 等待状态，释放mutex锁 通知条件变量wait的地方，可以起来干活了
	}
private:
	 std::atomic_bool isExit_;
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

// Task类型的前置声明
class Task;

// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);

	~Result() = default;

	// 问题一：setVal方法，获取任务执行完的返回值存放到any_里，通过信号量通知get()
	void setVal(Any any);

	// 问题二：get方法，用户调用这个方法获取task的返回值，通过信号量来阻塞
	Any get();
private:
	Any any_; // 存储任务的返回值
	Semaphore sem_; // 线程通信信号量,因为获取结果和任务执行是两个线程，如果任务执行未完成，则get()需要阻塞，因此需要线程通信
	std::shared_ptr<Task> task_; //指向对应获取返回值的任务对象，这样就不会在get()之前在另一个线程析构掉 
	std::atomic_bool isValid_; // 返回值是否有效，任务提交给线程池失败则get()不阻塞
};

// 任务抽象基类
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);//初始化result_

	// 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;

private:
	Result* result_; // Result对象的生命周期 》 Task的  用裸指针就可以了，不要用智能指针，否则会出现强智能指针的交叉引用问题，因为Result里放一个指向Task的智能指针，Task里放一个指向Result的强智能指针，否则Result和Task对象出了作用域也得不到释放，造成内存泄漏
};

// 线程池支持的模式
enum class PoolMode   //C++专门加了一个class，访问枚举项时加上枚举类型可避免不同枚举类型有相同的枚举项
{
	MODE_FIXED,  // 固定数量的线程
	MODE_CACHED, // 线程数量可动态增长
};

// 线程类型
class Thread
{
public:
	// 线程函数对象类型，using的另一用法
	using ThreadFunc = std::function<void(int)>;   

	// 线程构造
	Thread(ThreadFunc func);
	// 线程析构
	~Thread();
	// 启动线程
	void start();

	// 获取线程id
	int getId()const;
private:     
	ThreadFunc func_;
	static int generateId_;
	int threadId_;  // 保存线程id
};

/*api：
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
	public:
		void run() { // 线程代码... }
};

pool.submitTask(std::make_shared<MyTask>());             
*/
// 线程池类型
class  ThreadPool
{
public:
	// 线程池构造
	ThreadPool();

	// 线程池析构
	~ThreadPool();

	// 设置线程池的工作模式
	void setMode(PoolMode mode);

	// 设置task任务队列上线阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold);

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp); 

	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());    //默认为cpu核心数量

    //禁止用户对线程池本身进行拷贝和赋值，因为设计到容器、锁、条件变量等
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数。线程函数去任务队列里去抢任务来执行，因为其访问的任务队列的变量都在ThreadPool里，因此不定义在Thread里
	void threadFunc(int threadid);

	// 检查pool的运行状态
	bool checkRunningState() const;

private:
	// std::vector<std::unique_ptr<Thread>> threads_; // 线程列表为啥不用
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程列表,注意插入时的右值引用传递

	int initThreadSize_;  // 初始的线程数量
	int threadSizeThreshHold_; // 线程数量上限阈值（cached模式使用）
	std::atomic_int curThreadSize_;	// 记录当前线程池里面线程的总数量，为啥不直接用vector.size()呢？因为vector不是线程安全的
	std::atomic_int idleThreadSize_; // 记录空闲线程的数量

	std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列，为啥需要智能指针？因为防止用户传入的是一个短生命周期的对象，以及自动释放资源  
	std::atomic_int taskSize_; // 任务的数量   为啥不直接从taskQue_中获取大小，二要额外定义一个CAS呢
	int taskQueMaxThreshHold_;  // 任务队列数量上限阈值，每个任务都是要栈内存的，因此不能过多。

	std::mutex taskQueMtx_; // 保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable  notEmpty_; // 表示任务队列不空   生产者消费者模型为啥要用到两个条件变量呢？为了控制能更精细一点，见正式版284行：如果任务队列还有东西先唤醒其它线程
	std::condition_variable exitCond_; // 等到线程资源全部回收

	PoolMode poolMode_; // 当前线程池的工作模式
	std::atomic_bool isPoolRunning_; // 表示当前线程池的启动状态，防止用户调用start后再改变模式
};

#endif
