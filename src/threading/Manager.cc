
#include "Manager.h"
#include "NetVar.h"

using namespace threading;

Manager::Manager()
	{
	DBG_LOG(DBG_THREADING, "Creating thread manager ...");

	did_process = true;
	next_beat = 0;
	terminating = false;
	idle = true;

	heart_beat_interval = double(BifConst::Threading::heart_beat_interval);
	DBG_LOG(DBG_THREADING, "Heart beat interval set to %f", heart_beat_interval);
	}

Manager::~Manager()
	{
	if ( all_threads.size() )
		Terminate();
	}

void Manager::Terminate()
	{
	DBG_LOG(DBG_THREADING, "Terminating thread manager ...");

	terminating = true;

	// First process remaining thread output for the message threads.
	do Process(); while ( did_process );

	// Signal all to stop.
	for ( all_thread_list::iterator i = all_threads.begin(); i != all_threads.end(); i++ )
		(*i)->Stop();

	// Then join them all.
	for ( all_thread_list::iterator i = all_threads.begin(); i != all_threads.end(); i++ )
		{
		(*i)->Join();
		delete *i;
		}

	all_threads.clear();
	msg_threads.clear();

	idle = true;
	closed = true;
	terminating = false;
	}

void Manager::KillThreads()
	{
	DBG_LOG(DBG_THREADING, "Killing threads ...");

	for ( all_thread_list::iterator i = all_threads.begin(); i != all_threads.end(); i++ )
		(*i)->Kill();
	}

void Manager::AddThread(BasicThread* thread)
	{
	if ( heart_beat_interval == 0 ) {
		// Sometimes initialization does not seem to work from constructor.
		heart_beat_interval = double(BifConst::Threading::heart_beat_interval);
		DBG_LOG(DBG_THREADING, "Heart beat interval set to %f", heart_beat_interval);
	}

	DBG_LOG(DBG_THREADING, "Adding thread %s ...", thread->Name().c_str());
	all_threads.push_back(thread);
	idle = false;
	}

void Manager::AddMsgThread(MsgThread* thread)
	{
	DBG_LOG(DBG_THREADING, "%s is a MsgThread ...", thread->Name().c_str());
	msg_threads.push_back(thread);
	}

void Manager::GetFds(int* read, int* write, int* except)
	{
	}

double Manager::NextTimestamp(double* network_time)
	{
//	fprintf(stderr, "N %.6f %.6f did_process=%d next_next=%.6f\n", ::network_time, timer_mgr->Time(), (int)did_process, next_beat);

	if ( ::network_time && (did_process || ::network_time > next_beat || ! next_beat) )
		// If we had something to process last time (or out heartbeat
		// is due or not set yet), we want to check for more asap.
		return timer_mgr->Time();

	for ( msg_thread_list::iterator i = msg_threads.begin(); i != msg_threads.end(); i++ )
		{
			if ( (*i)->MightHaveOut() )
				return timer_mgr->Time();
		}

	return -1.0;
	}

void Manager::Process()
	{
	bool do_beat = false;

	if ( network_time && (network_time > next_beat || ! next_beat) )
		{
		do_beat = true;
		next_beat = ::network_time + heart_beat_interval;
		}

	did_process = false;

	for ( msg_thread_list::iterator i = msg_threads.begin(); i != msg_threads.end(); i++ )
		{
		MsgThread* t = *i;

		if ( do_beat )
			t->Heartbeat();

		while ( t->HasOut() )
			{
			Message* msg = t->RetrieveOut();

			if ( msg->Process() )
				{
				if ( network_time )
					did_process = true;
				}

			else
				{
				string s = msg->Name() + " failed, terminating thread";
				reporter->Error("%s", s.c_str());
				t->Stop();
			}

			delete msg;
			}
		}

//	fprintf(stderr, "P %.6f %.6f do_beat=%d did_process=%d next_next=%.6f\n", network_time, timer_mgr->Time(), do_beat, (int)did_process, next_beat);
	}

const threading::Manager::msg_stats_list& threading::Manager::GetMsgThreadStats()
	{
	stats.clear();

	for ( msg_thread_list::iterator i = msg_threads.begin(); i != msg_threads.end(); i++ )
		{
		MsgThread* t = *i;

		MsgThread::Stats s;
		t->GetStats(&s);

		stats.push_back(std::make_pair(t->Name(),s));
		}

	return stats;
	}


