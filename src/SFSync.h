#pragma once

namespace pioneer
{
	struct SFThread;
	class SFSync;
	typedef void(*SFFunc)(SFSync* sync, SFThread* thread, void* param);

	struct SFMsg
	{
		int _id;
		long long _llparam;
		float _fparam;
		double _dparam;
	};

	class SFSync
	{
	public:
		SFSync();
		~SFSync();
		bool Init();
		bool Uninit();

		bool Test();
		bool Loop(SFThread* thread);
		bool Poll(SFThread* thread, SFMsg& msg);
		bool Spawn(const char* name, SFFunc func, void* param);

		bool Send(const SFMsg& msg);
		bool Error(int code, const char* error);
		bool Term();

		static long long ErrorCode();
		static const char* ErrorMsg();

	private:
		class SFSyncImpl;
		SFSyncImpl* _impl;


		/*
		void device()
		{
		SFSync sync;

		if (sync.Test())
		{

		}
		}
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


