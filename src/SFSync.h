#pragma once

#include "SDL.h"
#include "SFMutex.h"
#include <string>
#include <vector>
#include <list>

namespace pioneer
{
	struct SFMsg;
	struct SFThread;
	class SFSync;
	typedef void(*SFFunc)(SFSync* sync, void* param);

	enum SFState
	{
		kStatePoll = 0,
		kStateLoop = 1,
		kStateTerm = 2,
	};

	struct SFMsg
	{
		int _id;
		long long _param;
	};

	struct SFThread
	{
		SFSync* _sync;
		SDL_Thread* _thread;
		std::string _name;
		SFFunc _func;
		void* _param;
		SFState _state;
	};

	class SFSync
	{
	public:
		SFSync();
		~SFSync();

		bool Test();
		bool Loop(SFThread* thread);
		bool Poll(SFThread* thread, SFMsg& msg);
		bool Spawn(const char* name, SFFunc func, void* param);

		void Send(const SFMsg& msg);
		void Error(int code, const char* error);
		void Term();

		static long long ErrorCode();
		static const char* ErrorMsg();

	private:
		SFMutex _mutex;
		std::vector<SFThread> _threads;
		std::list<SFMsg> _msgs;
		SFState _state;


		void device()
		{
			SFSync sync;

			if (sync.Test())
			{

			}
		}

		/*
		void Init()
		{
			SFSync _sync;
			_sync.CreateThread("Main", main, _impl);
		}

		void Pause()
		{
			_sync.Post(Pause);
		}

		void Uninit()
		{
			_sync.Term();
		}

		void decode(SFSync& sync, void* param)
		{

		}

		void handler(SFSync& sync, void* param)
		{
			Desc desc;
			SFMsg* msg = NULL;
			while (sync.Poll(msg))
			{
				if (msg.id == Stop)
				{

				}
			}
		}

		void main(SFSync& sync, void* param)
		{
			Desc desc;

			if (error && sync.Error(-2, ""))
				goto end;
			
			if (sync.CreateThread("Decode", decode, desc) == false && sync.Error(-3, ""))
				goto end;
			
			SFSyncMsg* msg = NULL;
			while (sync.Loop())
			{
				SDL_Event event;
				if (SDL_PollEvent(&event))
				{
					switch (event.type)
					{
					case SDL_QUIT:
						sync.Term();
						break;
					};
				}
			}
		end:




		}

		void decode()
		{
			while (sync.Loop())
			{



				if (error && sync.Error(-1, "error"))
					continue;
			}
		}

	private:
	*/
	};
	
}


