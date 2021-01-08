#pragma once

namespace pioneer
{

	class SFMutex
	{
	public:
		SFMutex();
		~SFMutex();
		void Enter(bool yieldSpin = true);
		void Leave();

	private:
		class SFMutexImpl;
		SFMutexImpl* _impl;
	};

	class SFMutexScoped
	{
	public:
		SFMutexScoped(SFMutex* mutex, bool yield = true);
		~SFMutexScoped();

		void LeaveEnter(bool yield = true);
	private:
		SFMutex* _mutex;
	};
}


