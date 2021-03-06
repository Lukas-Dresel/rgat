/*
Copyright 2016-2017 Nia Catlin

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
Class that is notified of and records thread/process stops/starts
Intended to be used to provide a visualisation
*/

#include "stdafx.h"
#include "timeline.h"
#include "traceMisc.h"
#include "OSspecific.h"

using namespace rapidjson;

timeline::timeline()
{
}


timeline::~timeline()
{
}

//return if there are still other processes running
bool timeline::notify_pid_end(PID_TID pid, int PID_ID)
{
	if (std::find(pidlist.begin(), pidlist.end(), make_pair(pid, PID_ID)) == pidlist.end())
	{
		cerr << "[rgat]ERROR: UNKNOWN PID" << endl;
		return false;
	}
	processEvent ev;
	ev.eventType = eProcessTerminate;
	ev.PID = pid;
	ev.PID_ID = PID_ID;
	time(&ev.eventTime);

	logLock.lock();
	eventLog.push_back(ev);
	logLock.unlock();

	--liveProcesses;

	return (liveProcesses > 0);

}

void timeline::notify_new_pid(PID_TID pid, int PID_ID, PID_TID parentPID)
{
	if (std::find(pidlist.begin(), pidlist.end(), make_pair(pid, PID_ID)) != pidlist.end())
	{
		cerr << "[rgat]ERROR: Duplicate PID" << endl;
		assert(false);
	}

	processEvent ev;
	ev.eventType = eTimelineEvent::eProcessCreate;
	ev.PID = pid;
	ev.PID_ID = PID_ID;
	ev.parentPID = parentPID;
	time(&ev.eventTime);

	logLock.lock();
	eventLog.push_back(ev);
	pidlist.push_back(make_pair(pid,PID_ID));
	logLock.unlock();
	++liveProcesses;
}

void timeline::notify_new_thread(PID_TID pid, int PID_ID, PID_TID tid)
{
	processEvent ev;
	ev.eventType = eTimelineEvent::eThreadCreate;
	time(&ev.eventTime);
	ev.PID = pid;
	ev.PID_ID = PID_ID;
	ev.TID = tid;

	logLock.lock();

	eventLog.push_back(ev);
	//pidlist[pid].push_back(&ev);
	logLock.unlock();
	++liveThreads;
}

void timeline::notify_thread_end(PID_TID pid, int PID_ID, PID_TID tid)
{
	processEvent ev;
	ev.eventType = eTimelineEvent::eThreadTerminate;
	time(&ev.eventTime);
	ev.PID = pid;
	ev.PID_ID = PID_ID;
	ev.TID = tid;

	logLock.lock();
	eventLog.push_back(ev);
	logLock.unlock();
	--liveThreads;
}

bool timeline::getFirstEventTime(time_t *result)
{
	if (eventLog.empty())
		return false;

	logLock.lock();
	*result = eventLog.front().eventTime;
	logLock.unlock();

	return true;
}

void serialiseEvent(rapidjson::Writer<FileWriteStream> &writer, processEvent &pevent)
{
	writer.StartArray();
	writer.Int(pevent.eventType);
	writer.Uint64(pevent.eventTime);
	writer.Uint64(pevent.PID);
	writer.Int(pevent.PID_ID);
	writer.Uint64(pevent.parentPID);
	writer.Uint64(pevent.TID);
	writer.EndArray();
}

void timeline::serialise(Writer<FileWriteStream> &writer)
{
	processEvent pevent;

	writer.StartObject();
	writer.Key("EventLog");

	writer.StartArray();

	logLock.lock();
	foreach(pevent, eventLog)
		serialiseEvent(writer, pevent);
	logLock.unlock();

	writer.EndArray();

	writer.EndObject();
}

//assumes loglog is held
bool timeline::unserialiseEvent(const Value& eventData)
{
	if (eventData.Capacity() != 6)
	{
		cerr << "[rgat] Bad timeline event entry" << endl;
		return false;
	}

	processEvent newEvent;
	newEvent.eventType = (eTimelineEvent)eventData[0].GetInt();
	newEvent.eventTime = eventData[1].GetUint64();
	newEvent.PID = eventData[2].GetUint64();
	newEvent.PID_ID = eventData[3].GetInt();
	newEvent.parentPID = eventData[4].GetUint64();
	newEvent.TID = eventData[5].GetUint64();

	eventLog.push_back(newEvent);

	pair<PID_TID, int> uniquePidPair = make_pair(newEvent.PID, newEvent.PID_ID);
	if (std::find(pidlist.begin(), pidlist.end(), uniquePidPair) == pidlist.end())
	{
		pidlist.push_back(uniquePidPair);
	}

	return true;
}

bool timeline::unserialise(const Value& timelineJSONData)
{
	processEvent pevent;

	logLock.lock();

	Value::ConstMemberIterator timelineDataIt = timelineJSONData.FindMember("EventLog");
	if (timelineDataIt == timelineJSONData.MemberEnd())
		return false;

	const Value& eventArray = timelineDataIt->value;

	Value::ConstValueIterator eventArrayIt = eventArray.Begin();
	for (; eventArrayIt != eventArray.End(); eventArrayIt++)
	{
		if (!unserialiseEvent(*eventArrayIt))
		{
			cerr << "[rgat] Failed to unserialise timeline event" << endl;
			return false;
		}
	}

	logLock.unlock();
	return true;
}