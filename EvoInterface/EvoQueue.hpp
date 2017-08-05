#ifndef _EVO_QUEUE_HPP__
#define _EVO_QUEUE_HPP__

#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>

template <class Type>
inline void DefaultDelete(Type **p)
{
	if (!p)
		return;
	if(*p != NULL) delete (Type*)*p;
	*p = nullptr;
}

template <class Type, void Default(Type **p) = DefaultDelete >
class EvoQueue 
{
public:
	EvoQueue(int maxLen)
		: maxLen(maxLen)
	{
	}

	~EvoQueue()
	{
	}

	Type* Dequeue()
	{
		if (forceQuit)
			return nullptr;

		std::unique_lock<std::mutex> lock(listMutex);
		if (dataList.empty())
			emptyCondition.wait(lock);
		if (dataList.empty())
			return nullptr;
		if (isStop && forceQuit)
			return nullptr;
		Type* p = dataList.front();
		dataList.pop_front();
		--curLen;
		fullCondition.notify_one();
		return p;
	}

	Type* Dequeue( int timeOutMilliseconds)
	{
		if (forceQuit)
			return nullptr;

		std::unique_lock<std::mutex> lock(listMutex);
		if (dataList.empty()) 
		{
			std::cv_status ret = emptyCondition.wait_for(lock, std::chrono::milliseconds(timeOutMilliseconds));
			if (ret == std::cv_status::timeout)
				return nullptr;
		}
		if (dataList.empty())
		{
			return nullptr;
		}
		if (isStop && forceQuit)
			return nullptr;
		Type* p = dataList.front();
		dataList.pop_front();
		--curLen;
		fullCondition.notify_one();
		return p;
	}

	int Count()
	{
		std::unique_lock<std::mutex> lock(listMutex);
		return (int)curLen;
	}

	bool Enqueue(Type* p)
	{
		if (isStop || forceQuit)
			return false;

		std::unique_lock<std::mutex> lock(listMutex);
		if (dataList.size() >= maxLen)
			fullCondition.wait(lock);
		if (isStop && forceQuit)
			return false;
		dataList.push_back(p);
		++curLen;
		emptyCondition.notify_one();
		return true;
	}

	bool Enqueue(Type* p, int timeOutMilliseconds)
	{
		if (isStop || forceQuit)
			return false;

		std::unique_lock<std::mutex> lock(listMutex);
		if (dataList.size() >= maxLen)
			if (fullCondition.wait_for(lock, std::chrono::milliseconds(timeOutMilliseconds)) == std::cv_status::timeout)
				return false;
		if (isStop && forceQuit)
			return false;
		dataList.push_back(p);
		++curLen;
		emptyCondition.notify_one();
		return true;
	}

	void Clear(bool forceQuit)
	{
		isStop = true;
		forceQuit = forceQuit;
		fullCondition.notify_all();
		emptyCondition.notify_all();
		if (curLen != 0 && !forceQuit)
		{
			std::unique_lock<std::mutex> lock(listMutex);
			emptyCondition.wait(lock);
			return;
		}
		std::unique_lock<std::mutex> lock(listMutex);
		while (!dataList.empty())
		{
			Type* p = dataList.front();
			dataList.pop_front();
			Default((Type**)&p);
		}
		curLen = 0;
	}

	void Notify(bool forceQuit)
	{
		isStop = true;
		forceQuit = forceQuit;
		fullCondition.notify_all();
		emptyCondition.notify_all();
	}

	void Restart()
	{
		isStop = false;
		forceQuit = false;
	}

private:
	std::list<Type*> dataList;
	size_t maxLen = 0;
	size_t curLen = 0;
	std::mutex listMutex;
	std::condition_variable fullCondition;
	std::condition_variable emptyCondition;

	bool isStop = false;
	bool forceQuit = false;
};

#endif//_EVO_QUEUE_HPP__