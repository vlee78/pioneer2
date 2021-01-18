#pragma once

namespace pioneer
{

	class SFMutex
	{
	public:
		SFMutex();
		~SFMutex();
		void Enter(bool yieldSpin = false);
		void Leave();

	private:
		class SFMutexImpl;
		SFMutexImpl* _impl;
	};

	class SFMutexScoped
	{
	public:
		SFMutexScoped(SFMutex* mutex, bool yield = false);
		~SFMutexScoped();

		void LeaveEnter(bool yield = true);
	private:
		SFMutex* _mutex;
	};
}


