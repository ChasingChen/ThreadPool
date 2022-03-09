 #ifndef THREADPOOL_H
#define THREADPOOL_H
//�����淶���������շ�ʽ���»��߷ź��棨��ΪC++����ܶ඼�ǰ��»��߷�ǰ��ģ�
//��������ĸ��д
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>

// Any���ͣ����Խ����������ݵ�����
class Any    //ģ��Ĵ��붼д��һ���ļ���
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;   //ΪɶҪ=delete,unique_ptr��auto_ptr��֮ͬ�����ڣ�unique_ptr��Զֻ��һ������ָ��ָ����Դ��������ֵ���ÿ������츳ֵ�����õ��ˣ���������ֵ���ÿ�������
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// ������캯��������Any���ͽ�����������������
	template<typename T>  // T:int    Derive<int>
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	// ��������ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// ������ô��base_�ҵ�����ָ���Derive���󣬴�������ȡ��data��Ա����
		// ����ָ�� =�� ������ָ��   RTTI��Run-Time Type Identification):����ǿתֻ��dynamic_castͨ������ʱ������Ϣ�����ܹ�ʹ�û����ָ��������������Щָ���������ָ�Ķ����ʵ����������
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());  //����ָ��get()��ȡ��ָ��
		if (pd == nullptr)   //����Any����ʱ����intʵ�������󣬻���ָ��ָ��Derive<int>,��ǿת��ͼתΪlong��Ҫ��ʾ���ɹ�
		{
			throw "type is unmatch!";
		}
		return pd->data_;  //��������ĳ�Ա��������
	}
private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;  //�����ñ������Ż���{}=default��Ĭ��ʵ�� 
	};

	// ����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) 
		{}
		T data_;  // �������������������
	};

private:
	// ����һ�������ָ��
	std::unique_ptr<Base> base_;
};

// ʵ��һ���ź�����
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

	// ��ȡһ���ź�����Դ
	void wait()
	{
		if(isExit_)
		return;
		std::unique_lock<std::mutex> lock(mtx_);
		// �ȴ��ź�������Դ��û����Դ�Ļ�����������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// ����һ���ź�����Դ
	void post()
	{
		if(isExit_)
		return;
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// linux��condition_variable����������ʲôҲû��
		// ��������״̬�Ѿ�ʧЧ���޹�����
		cond_.notify_all();  // �ȴ�״̬���ͷ�mutex�� ֪ͨ��������wait�ĵط������������ɻ���
	}
private:
	 std::atomic_bool isExit_;
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

// Task���͵�ǰ������
class Task;

// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);

	~Result() = default;

	// ����һ��setVal��������ȡ����ִ����ķ���ֵ��ŵ�any_�ͨ���ź���֪ͨget()
	void setVal(Any any);

	// �������get�������û��������������ȡtask�ķ���ֵ��ͨ���ź���������
	Any get();
private:
	Any any_; // �洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ���ź���,��Ϊ��ȡ���������ִ���������̣߳��������ִ��δ��ɣ���get()��Ҫ�����������Ҫ�߳�ͨ��
	std::shared_ptr<Task> task_; //ָ���Ӧ��ȡ����ֵ��������������Ͳ�����get()֮ǰ����һ���߳������� 
	std::atomic_bool isValid_; // ����ֵ�Ƿ���Ч�������ύ���̳߳�ʧ����get()������
};

// ����������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);//��ʼ��result_

	// �û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;

private:
	Result* result_; // Result������������� �� Task��  ����ָ��Ϳ����ˣ���Ҫ������ָ�룬��������ǿ����ָ��Ľ����������⣬��ΪResult���һ��ָ��Task������ָ�룬Task���һ��ָ��Result��ǿ����ָ�룬����Result��Task�������������Ҳ�ò����ͷţ�����ڴ�й©
};

// �̳߳�֧�ֵ�ģʽ
enum class PoolMode   //C++ר�ż���һ��class������ö����ʱ����ö�����Ϳɱ��ⲻͬö����������ͬ��ö����
{
	MODE_FIXED,  // �̶��������߳�
	MODE_CACHED, // �߳������ɶ�̬����
};

// �߳�����
class Thread
{
public:
	// �̺߳����������ͣ�using����һ�÷�
	using ThreadFunc = std::function<void(int)>;   

	// �̹߳���
	Thread(ThreadFunc func);
	// �߳�����
	~Thread();
	// �����߳�
	void start();

	// ��ȡ�߳�id
	int getId()const;
private:     
	ThreadFunc func_;
	static int generateId_;
	int threadId_;  // �����߳�id
};

/*api��
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
	public:
		void run() { // �̴߳���... }
};

pool.submitTask(std::make_shared<MyTask>());             
*/
// �̳߳�����
class  ThreadPool
{
public:
	// �̳߳ع���
	ThreadPool();

	// �̳߳�����
	~ThreadPool();

	// �����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold);

	// ���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp); 

	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());    //Ĭ��Ϊcpu��������

    //��ֹ�û����̳߳ر�����п����͸�ֵ����Ϊ��Ƶ���������������������
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �����̺߳������̺߳���ȥ���������ȥ��������ִ�У���Ϊ����ʵ�������еı�������ThreadPool���˲�������Thread��
	void threadFunc(int threadid);

	// ���pool������״̬
	bool checkRunningState() const;

private:
	// std::vector<std::unique_ptr<Thread>> threads_; // �߳��б�Ϊɶ����
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б�,ע�����ʱ����ֵ���ô���

	int initThreadSize_;  // ��ʼ���߳�����
	int threadSizeThreshHold_; // �߳�����������ֵ��cachedģʽʹ�ã�
	std::atomic_int curThreadSize_;	// ��¼��ǰ�̳߳������̵߳���������Ϊɶ��ֱ����vector.size()�أ���Ϊvector�����̰߳�ȫ��
	std::atomic_int idleThreadSize_; // ��¼�����̵߳�����

	std::queue<std::shared_ptr<Task>> taskQue_; // ������У�Ϊɶ��Ҫ����ָ�룿��Ϊ��ֹ�û��������һ�����������ڵĶ����Լ��Զ��ͷ���Դ  
	std::atomic_int taskSize_; // ���������   Ϊɶ��ֱ�Ӵ�taskQue_�л�ȡ��С����Ҫ���ⶨ��һ��CAS��
	int taskQueMaxThreshHold_;  // �����������������ֵ��ÿ��������Ҫջ�ڴ�ģ���˲��ܹ��ࡣ

	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в���
	std::condition_variable  notEmpty_; // ��ʾ������в���   ������������ģ��ΪɶҪ�õ��������������أ�Ϊ�˿����ܸ���ϸһ�㣬����ʽ��284�У����������л��ж����Ȼ��������߳�
	std::condition_variable exitCond_; // �ȵ��߳���Դȫ������

	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_; // ��ʾ��ǰ�̳߳ص�����״̬����ֹ�û�����start���ٸı�ģʽ
};

#endif
