#include "threadpool_1.0.h"

#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // ��λ����

// �̳߳ع���
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// �̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
		notEmpty_.notify_all();
	// �ȴ��̳߳��������е��̷߳���  ������״̬��û���������� & ����ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });//�˴�Ҫ�ر�ע�����notifyһ�£���Ȼmain��������getchar()ִ�в���
}

// �����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())   
		return;
	poolMode_ = mode;
}

// ����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

// �����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;  
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

// ���̳߳��ύ����    �û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// �̵߳�ͨ��  �ȴ���������п���   wait   wait_for����������Ϊbool��   wait_until
	// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))   
	{
		// ��ʾnotFull_�ȴ�1s�֣�������Ȼû������
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return sp->getResult();  // Ϊʲô��������ĳ�Ա��������Result�ķ���ֵ�أ���Ϊִ���߳�ִ���������������ͱ��������ˣ�������̵߳�res�Ͳ���get()�����ʶ�����
		return Result(sp, false);
	}

	// ����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;

	// ��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	// cachedģʽ ������ȽϽ��� ������С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���,�����ݿ����ӳ��ǲ���
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> create new thread..." << std::endl;   //ע�⣺create new tread�ͻ�ȡ�������������߳�

		// �����µ��̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));   //Ϊɶ��Ҫ�󶨣���ΪThread����ֻ���ܺ���ָ�룬�������ܳ�Ա����
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// �����߳�
		threads_[threadId]->start(); 
		// �޸��̸߳�����صı���
		curThreadSize_++;
		idleThreadSize_++;
	}

	// ���������Result����
	return Result(sp);
	// return sp->getResult();
}

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// �����̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���(��Ϊ�̺߳����Ĺ��캯����Ҫ���뺯������)
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));    //ע��unique_ptr�����������ͨ�Ĺ���͸�ֵ�������תΪ��ֵ
		// threads_.emplace_back(std::move(ptr));
	}

	// ���������߳�  std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // ��Ҫȥִ��һ���̺߳���
		idleThreadSize_++;    // ��¼��ʼ�����̵߳�����
	}
}

// �����̺߳���   �̳߳ص������̴߳�������������������� 
void ThreadPool::threadFunc(int threadid)  // �̺߳������أ���Ӧ���߳�Ҳ�ͽ�����
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	// �����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// �Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "���Ի�ȡ����..." << std::endl;

			// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳�
			// �������յ�������initThreadSize_�������߳�Ҫ���л��գ�
			// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
			
			// ÿһ���з���һ��   ��ô���֣���ʱ���أ������������ִ�з���
			// �� + ˫���ж�
 			{
				// �̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadid); // std::this_thread::getid()
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitCond_.notify_all();
					return; // �̺߳����������߳̽���
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ������������ʱ������
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)  
						{ 
							// ��ʼ���յ�ǰ�߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳�����߳��б�������ɾ��   û�а취 threadFunc��=��thread����
							// threadid => thread���� => ɾ��
							threads_.erase(threadid); // std::this_thread::getid()
							curThreadSize_--;  
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}  
				}
				else
				{
					// �ȴ�notEmpty����
					notEmpty_.wait(lock);
				}

				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadid); // std::this_thread::getid()
				//	std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
				//		<< std::endl;
				//	exitCond_.notify_all();   //֪ͨһ��Thread����������������������û��
				//	return; // �����̺߳��������ǽ�����ǰ�߳���!
				//}
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "��ȡ����ɹ�..." << std::endl;

			// �����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ��һ�����񣬽���֪ͨ��֪ͨ���Լ����ύ�������� 
			notFull_.notify_all();
		} // �����Ӧ�ð����ͷŵ�������������ִ��������һ��forѭ��ǰ���ͷţ�����ֶ���������������
		
		// ��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			// task->run(); // ִ�����񣻰�����ķ���ֵsetVal��������Result
			task->exec();
		}
		
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // 
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

////////////////  �̷߳���ʵ��
int Thread::generateId_ = 0;

// �̹߳���
Thread::Thread(ThreadFunc func)
	: func_(func)    //���հ����󶨵��̺߳��� 
	, threadId_(generateId_++)
{}

// �߳�����
Thread::~Thread() {}

// �����߳�
void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳��� pthread_create
	std::thread t(func_, threadId_);  // C++11��˵ �̶߳���t  ���̺߳���func_
	t.detach(); // ���÷����̣߳�����̶߳�����̺߳����������̺߳�����ʧ��   pthread_detach  pthread_t���óɷ����߳�
}

int Thread::getId()const
{
	return threadId_;
}


/////////////////  Task����ʵ��
Task::Task()
	: result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{								//���ﲻ�õ���result_û�ˣ���Ϊtaskû��result��������
		result_->setVal(run()); // ���﷢����̬���ã����麯������һ���װ
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}

/////////////////   Result������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{
	task_->setResult(this);
}

Any Result::get() // �û����õ�
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // task�������û��ִ���꣬����������û�����get()���߳�
	return std::move(any_);  //��ΪAny��Ա������unique_ptr����û����ֵ���õĿ�������͸�ֵ����any_����ֵ����ֻ��ѡ��ȥ����һ���ֲ���ʱ��ֵ����move��any_תΪ��ֵ
}

void Result::setVal(Any any)  // ˭���õ��أ�����
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);  //anyû����ֵ���õĿ�������͸�ֵ����
	sem_.post(); // �Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}