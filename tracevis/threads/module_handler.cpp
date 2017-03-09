/*
Copyright 2016 Nia Catlin

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
This is the base thread for each instrumented process.
It reads module and symbol data for the process
It also launches trace reader and handler threads when the process spawns a thread
*/

#include "stdafx.h"
#include "module_handler.h"
#include "traceMisc.h"
#include "trace_handler.h"
#include "thread_trace_reader.h"
#include "sphere_graph.h"
#include "tree_graph.h"
#include "GUIManagement.h"
#include "b64.h"

//listen to mod data for given PID
void module_handler::main_loop()
{
	alive = true;
	pipename = wstring(L"\\\\.\\pipe\\rioThreadMod");
	pipename.append(std::to_wstring(PID));

	const wchar_t* szName = pipename.c_str();
	HANDLE hPipe = CreateNamedPipe(szName,
		PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_MESSAGE | PIPE_WAIT,
		255, 64, 56 * 1024, 0, NULL);

	OVERLAPPED ov = { 0 };
	ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	if (ConnectNamedPipe(hPipe, &ov))
	{
		wcerr << "[rgat]ERROR: Failed to ConnectNamedPipe to " << pipename << " for PID "<<PID<< ". Error: " << GetLastError();
		alive = false;
		return;
	}

	while (!die)
	{
		if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_TIMEOUT) break;
		wcerr << "[rgat]WARNING: Long wait for module handler pipe " << pipename << endl;
	}
	piddata->set_running(true);

	//if not launch by command line - do GUI stuff
	if (clientState->commandlineLaunchPath.empty())
	{
		TraceVisGUI* widgets = (TraceVisGUI *)clientState->widgets;
		widgets->addPID(PID);
	}

	char buf[400] = { 0 };
	int PIDcount = 0;

	OVERLAPPED ov2 = { 0 };
	ov2.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!ov2.hEvent)
	{
		cerr << "RGAT: ERROR - Failed to create overlapped event in module handler" << endl;
		assert(false);
	}

	vector < base_thread *> threadList;
	vector < thread_trace_reader *> readerThreadList;
	DWORD res= 0;
	while (!die && !piddata->should_die())
	{

		DWORD bread = 0;
		ReadFile(hPipe, buf, 399, &bread, &ov2);
		while (true)
		{
			if (WaitForSingleObject(ov2.hEvent, 300) != WAIT_TIMEOUT) break;
			if (die || piddata->should_die() || clientState->die) {
				die = true;
				break;
			}
		}

		if (GetLastError() != ERROR_IO_PENDING) continue;
		int res2 = GetOverlappedResult(hPipe, &ov2, &bread, false);
		buf[bread] = 0;
	
		if (!bread)
		{
			//not sure this ever gets called, read probably fails?
			int err = GetLastError();
			if (err != ERROR_BROKEN_PIPE && !die)
				cerr << "[rgat]ERROR. threadpipe ReadFile error: " << err << endl;
			alive = false;
			break;
		}
		else
		{	
			//thread created
			if (buf[0] == 'T' && buf[1] == 'I')
			{
				PID_TID TID = 0;
				if (!extract_tid(buf, string("TI"), &TID))
				{
					cerr << "[rgat] Fail to extract thread ID from TI tag:" << buf << endl;
					continue;
				}

				proto_graph *newProtoGraph = new proto_graph(piddata, TID);
				plotted_graph *newPlottedGraph = 0;
				switch (clientState->currentLayout)
				{
					case eSphereLayout:
					{
						newPlottedGraph = new sphere_graph(piddata, TID, newProtoGraph, &clientState->config->graphColours);
						break;
					}
					case eTreeLayout:
					{
						newPlottedGraph = new tree_graph(piddata, TID, newProtoGraph, &clientState->config->graphColours);
						
						break;
					}
					default:
					{	
						cout << "Bad graph layout: " << clientState->currentLayout << endl;
						assert(0); 
					}
				}
				newPlottedGraph->initialiseDefaultDimensions();

				//todo?
				//graph->set_max_wait_frames(clientState->config->maxWaitFrames); 
				

				thread_trace_reader *TID_reader = new thread_trace_reader(newProtoGraph, PID, TID);
				TID_reader->traceBufMax = clientState->config->traceBufMax;
				newProtoGraph->setReader(TID_reader);

				threadList.push_back(TID_reader);
				readerThreadList.push_back(TID_reader);
				rgat_create_thread(TID_reader->ThreadEntry, TID_reader);
				
				thread_trace_handler *TID_processor = new thread_trace_handler(newProtoGraph, PID, TID);
				TID_processor->piddata = piddata;
				TID_processor->reader = TID_reader;
				TID_processor->timelinebuilder = clientState->timelineBuilder;
				TID_processor->basicMode = clientState->launchopts.basic;
				TID_processor->set_max_arg_storage(clientState->config->maxArgStorage);
				TID_processor->saveFlag = &clientState->saving;

				if (!obtainMutex(piddata->graphsListMutex, 1010)) break;
				if (piddata->protoGraphs.count(TID) > 0)
					cerr << "[rgat]ERROR: Duplicate thread ID! Tell the dev to stop being awful" << endl;
				piddata->protoGraphs.insert(make_pair(TID, newProtoGraph));
				piddata->plottedGraphs.insert(make_pair(TID, newPlottedGraph));
				dropMutex(piddata->graphsListMutex);

				threadList.push_back(TID_processor);
				clientState->timelineBuilder->notify_new_tid(PID, TID);
				rgat_create_thread(TID_processor->ThreadEntry, TID_processor);
				continue;
			}

			//symbol
			if (buf[0] == 's' && buf[1] == '!' && bread > 8)
			{
				char *next_token = NULL;
				unsigned int modnum = atoi(strtok_s(buf + 2, "@", &next_token));
				if (modnum > piddata->modpaths.size()) {
					cerr << "[rgat]Bad mod number " << modnum << "in sym processing. " <<
						piddata->modpaths.size() << " exist." << endl;
					continue;
				}

				char *offset_s = strtok_s(next_token, "@", &next_token);
				MEM_ADDRESS address;
				sscanf_s(offset_s, "%llx", &address);

				if(!piddata->modBounds.count(modnum)) 
					cerr << "[rgat]Warning: sym before module. handle me"<<endl; //fail if sym came before module
				else
					address += piddata->modBounds[modnum].first;

				string symname = string(next_token);
				piddata->getDisassemblyWriteLock();
				piddata->modsymsPlain[modnum][address] = symname;
				piddata->dropDisassemblyWriteLock();
				continue;
			}

			//module/dll
			if (buf[0] == 'm' && buf[1] == 'n' && bread > 8)
			{
				char *next_token = NULL;

				string b64path;
				//null path
				if (buf[2] == '@' && buf[3] == '@')
				{
					next_token = buf + 4; //skip past 'mn@@'
				}
				else 
					b64path = string(strtok_s(buf + 2, "@", &next_token));

				char *modnum_s = strtok_s(next_token, "@", &next_token);
				long modnum = -1;
				sscanf_s(modnum_s, "%d", &modnum);

				piddata->getDisassemblyReadLock();
				if (piddata->modpaths.count(modnum) > 0) {
					cerr<< "[rgat]ERROR: PID:"<<PID<<" Bad(prexisting) module number "<<modnum<<" in mn ["<<
						buf<<"]. current is:" << piddata->modpaths.at(modnum) << endl;
					assert(0);
				}
				piddata->dropDisassemblyReadLock();

				//todo: safe stol? if this is safe whytf have i implented safe stol
				char *startaddr_s = strtok_s(next_token, "@", &next_token);
				MEM_ADDRESS startaddr = 0;
				sscanf_s(startaddr_s, "%llx", &startaddr);

				char *endaddr_s = strtok_s(next_token, "@", &next_token);
				MEM_ADDRESS endaddr = 0;
				sscanf_s(endaddr_s, "%llx", &endaddr);


				char *skipped_s = strtok_s(next_token, "@", &next_token);
				piddata->getDisassemblyWriteLock();

				if (*skipped_s == '1')
					piddata->activeMods[modnum] = MOD_UNINSTRUMENTED;
				else
					piddata->activeMods[modnum] = MOD_INSTRUMENTED;

				piddata->dropDisassemblyWriteLock();

				if (!startaddr | !endaddr | (next_token - buf != bread)) {
					cerr << "ERROR! Processing module line: "<< buf << endl;
					assert(0);
				}
				
				string path_plain;
				if (!b64path.empty())
					path_plain = base64_decode(b64path);
				else
					path_plain = "";

				piddata->getDisassemblyWriteLock();
				piddata->modpaths[modnum] = path_plain;
				
				if (!piddata->modBounds.count(modnum) && piddata->modsymsPlain.count(modnum))
					{
						cerr << "[rgat]ERROR: module after sym - add address to all relevant syms" << endl;
						assert(0);
					}

				piddata->modBounds[modnum] = make_pair(startaddr, endaddr);
				piddata->dropDisassemblyWriteLock();

				continue;
			}
		}
	}

	//exited loop, first get readers to terminate
	vector <thread_trace_reader *>::iterator threadIt = readerThreadList.begin();
	for (; threadIt != readerThreadList.end(); ++threadIt)
		((thread_trace_reader *)(*threadIt))->kill();

	//wait for both readers and processors to terminate as a result
	vector <base_thread *>::iterator athreadIt = threadList.begin();
	for (athreadIt = threadList.begin(); athreadIt != threadList.end(); ++athreadIt)
	{
		while ((base_thread *)(*athreadIt)->is_alive())
			Sleep(1);
	}

	clientState->timelineBuilder->notify_pid_end(PID);
	piddata->set_running(false); //the process is done
	alive = false; //this thread is done
}