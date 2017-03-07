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
The thread that performs low (ie:periodic) performance rendering of all graphs for the preview pane
*/
#include <stdafx.h>
#include "render_heatmap_thread.h"
#include "traceMisc.h"
#include "rendering.h"

bool heatmap_renderer::render_graph_heatmap(plotted_graph *graph, bool verbose)
{
	proto_graph *protoGraph = graph->get_protoGraph();
	if (!protoGraph->get_num_edges()) return false;

	GRAPH_DISPLAY_DATA *linedata = graph->get_mainlines();
	unsigned int numLineVerts;
	if (linedata)
	{
		numLineVerts = linedata->get_numVerts();

		//graph never been rendered so we cant get the edge vertex data to colourise it
		if (!numLineVerts)
			if (!protoGraph->active && graph != clientState->activeGraph)
				graph->render_static_graph(clientState); //got final data so may as well force rendering
			else
				return false;
	} 
	else return false; 

	DWORD this_run_marker = GetTickCount();

	map <NODEINDEX,bool> errorNodes;

	//build set of all heat values
	std::set<unsigned long> heatValues;
	EDGEMAP::iterator edgeDit, edgeDEnd;

	vector<pair<NODEPAIR,edge_data *>> unfinishedEdgeList;
	vector<edge_data *> finishedEdgeList;

	unsigned int solverErrors = 0;
	protoGraph->start_edgeD_iteration(&edgeDit, &edgeDEnd);
	for (; edgeDit != edgeDEnd; ++edgeDit)
	{
		node_data *snode = protoGraph->safe_get_node(edgeDit->first.first);
		node_data *tnode = protoGraph->safe_get_node(edgeDit->first.second);
		edge_data *edge = &edgeDit->second;

		//initialise temporary counters
		if (snode->heat_run_marker != this_run_marker)
		{
			snode->chain_remaining_in = snode->executionCount;
			snode->chain_remaining_out = snode->executionCount;
			snode->heat_run_marker = this_run_marker;
		}
		if (tnode->heat_run_marker != this_run_marker)
		{
			tnode->chain_remaining_in = tnode->executionCount;
			tnode->chain_remaining_out = tnode->executionCount;
			tnode->heat_run_marker = this_run_marker;
		}

		//the easiest edges to work out are the most numerous
		//this node is only followed by 1 node therefore edge weight is this nodes executions
		if (snode->outgoingNeighbours.size() == 1)
		{
			edge->chainedWeight = snode->executionCount;
			snode->chain_remaining_out = 0;
			//does instruction execute more times than the only instruction that follows it?
			if (snode->executionCount > tnode->chain_remaining_in)
			{
				//ignore if the instruction was the last in the thread and the difference is 1
				if ((snode->index != protoGraph->finalNodeID) && (snode->executionCount != (tnode->chain_remaining_in + 1)))
				{
					++solverErrors;
					if (verbose && errorNodes.count(snode->index) == 0)
					{
						cerr << "[rgat]Heat solver warning 1: (TID" << dec << protoGraph->get_TID() << "): Sourcenode:" << snode->index <<
							" (only 1 target) has " << snode->executionCount << " output but targnode " << tnode->index <<
							" only needs " << tnode->chain_remaining_in << endl;
						errorNodes[snode->index] = true;
					}
				}
			}
			tnode->chain_remaining_in -= snode->executionCount;
			finishedEdgeList.push_back(edge);
		}
		else if (tnode->incomingNeighbours.size() == 1)
		{
			//this node only follows one other node therefore edge weight is its executions
			edge->chainedWeight = tnode->executionCount;
			tnode->chain_remaining_in = 0;
			//this instruction executed more than the only instruction that leads to it?
			if (tnode->executionCount > snode->chain_remaining_out)
			{
				++solverErrors;
				if (verbose && errorNodes.count(tnode->index) == 0)
				{
					cerr << "[rgat]Heat solver warning 2: (TID" << dec << protoGraph->get_TID() << "): Targnode:" << tnode->index
						<< " (only only 1 caller) needs " << tnode->executionCount <<	" in but sourcenode (" 
						<< snode->index << ") only provides " << snode->chain_remaining_out << " out" << endl;
					errorNodes[tnode->index] = true;
				}
			}
			snode->chain_remaining_out -= tnode->executionCount;
			finishedEdgeList.push_back(edge);
		}
		else
		{
			edge->chainedWeight = 0;
			unfinishedEdgeList.push_back(make_pair(edgeDit->first, &edgeDit->second));
		}
	}
	protoGraph->stop_edgeD_iteration();

	//this won't work until nodes have correct values
	//it's a great way of detecting errors in a compelete graph but in a running graph there are always going
	//to be discrepancies. still want to have a vaguely accurate heatmap in realtime though
	int attemptLimit = 5;
	vector<pair<NODEPAIR, edge_data *>>::iterator unfinishedIt;
	while (!unfinishedEdgeList.empty() && attemptLimit--)
	{
		unfinishedIt = unfinishedEdgeList.begin();
		for (; unfinishedIt != unfinishedEdgeList.end(); ++unfinishedIt)
		{
			unsigned int srcNodeIdx = unfinishedIt->first.first;
			unsigned int targNodeIdx = unfinishedIt->first.second;
			

			//see if targets other inputs have remaining output
			unsigned long targOtherNeighboursOut = 0;

			protoGraph->acquireNodeReadLock();
			node_data *tnode = protoGraph->unsafe_get_node(targNodeIdx);
			node_data *snode = protoGraph->unsafe_get_node(srcNodeIdx);

			set<unsigned int>::iterator targincomingIt = tnode->incomingNeighbours.begin();
			for (; targincomingIt != tnode->incomingNeighbours.end(); targincomingIt++)
			{
				unsigned int idx = *targincomingIt;
				if (idx == srcNodeIdx) continue; 
				node_data *neib = protoGraph->unsafe_get_node(idx);
				targOtherNeighboursOut += neib->chain_remaining_out;
			}
			
			//no? only source node giving input. complete edge and subtract from source output 
			if (targOtherNeighboursOut == 0)
			{
				//only node with executions remaining has less executions than this node?
				if (tnode->chain_remaining_in > snode->chain_remaining_out)
				{
					++solverErrors;
					if (verbose && errorNodes.count(tnode->index) == 0)
					{
						cerr << "[rgat]Heat solver warning 3: (TID" << dec << protoGraph->get_TID() << "): Targnode  " << tnode->index <<
							" has only one adjacent providing output, but needs more (" << tnode->chain_remaining_in << ") than snode (" 
							<< snode->index <<") provides (" << snode->chain_remaining_out << ")" << endl;
						errorNodes[tnode->index] = true;
					}
				}
				else
				{
					edge_data *edge = unfinishedIt->second;
					edge->chainedWeight = tnode->chain_remaining_in;
					tnode->chain_remaining_in = 0;
					snode->chain_remaining_out -= edge->chainedWeight;

					finishedEdgeList.push_back(edge);
					unfinishedIt = unfinishedEdgeList.erase(unfinishedIt);
					attemptLimit++;

					protoGraph->releaseNodeReadLock();
					break;
				}
			}

			protoGraph->releaseNodeReadLock();

			//see if other source outputs need input
			unsigned long sourceOtherNeighboursIn = 0;

			protoGraph->acquireNodeReadLock();
			set<unsigned int>::iterator sourceoutgoingIt = snode->outgoingNeighbours.begin();
			for (; sourceoutgoingIt != snode->outgoingNeighbours.end(); sourceoutgoingIt++)
			{
				unsigned int idx = *sourceoutgoingIt;
				if (idx == tnode->index) continue;
				node_data *neib = protoGraph->unsafe_get_node(idx);
				sourceOtherNeighboursIn += neib->chain_remaining_in;
			}

			protoGraph->releaseNodeReadLock();

			//no? only targ edge taking input. complete edge and subtract from targ input 
			if (sourceOtherNeighboursIn == 0)
			{
				//only remaining folllower node executed less than this node did
				if (snode->chain_remaining_out > tnode->chain_remaining_in)
				{
					++solverErrors;
					if (verbose && errorNodes.count(snode->index) == 0)
					{
						cerr << "[rgat]Heat solver warning 4: (TID" << dec << protoGraph->get_TID() << ") : Sourcenode " << snode->index
							<< " has one adjacent taking input, but has more (" << snode->chain_remaining_out << ") than the targnode (" 
							<< tnode->index << ") needs (" << tnode->chain_remaining_in << ")" << endl;	
						errorNodes[snode->index] = true;
					}
				}
				else
				{
					edge_data *edge = unfinishedIt->second;
					edge->chainedWeight = snode->chain_remaining_out;
					snode->chain_remaining_out = 0;
					tnode->chain_remaining_in -= edge->chainedWeight;

					finishedEdgeList.push_back(edge);
					unfinishedIt = unfinishedEdgeList.erase(unfinishedIt);
					++attemptLimit;
					break;
				}
			}	
		}
	}


	if (verbose)
	{
		if (!unfinishedEdgeList.empty() || solverErrors)
			cout << "[rgat]Heatmap Failure for thread " << dec << protoGraph->get_TID() << ": Ending solver with "<<
			unfinishedEdgeList.size() << " unsolved / " << dec << solverErrors <<" errors. Trace may have inaccuracies (eg: due to unexpected termination)." << endl;
		else
			cout << "[rgat]Heatmap Success for thread " << dec << protoGraph->get_TID() << ": Ending solver with "<< finishedEdgeList.size() <<" solved edges. Trace likely accurate."<<endl;
	}

	//build set of all heat values
	vector<edge_data *>::iterator finishedEdgeIt;
	for (finishedEdgeIt = finishedEdgeList.begin(); finishedEdgeIt != finishedEdgeList.end(); ++finishedEdgeIt)
		heatValues.insert(((edge_data *)*finishedEdgeIt)->chainedWeight);

	int heatrange = heatValues.size();

	//create map of distances of each value in set
	map<unsigned long, int> heatDistances;
	set<unsigned long>::iterator setit;
	int distance = 0;
	for (setit = heatValues.begin(); setit != heatValues.end(); ++setit)
		heatDistances[*setit] = distance++;
	graph->heatExtremes = make_pair(*heatValues.begin(),*heatValues.rbegin());

	int maxDist = heatDistances.size();
	map<unsigned long, int>::iterator distit = heatDistances.begin();
	map<unsigned long, COLSTRUCT> heatColours;
	
	//create blue->red value for each numerical 'heat'
	int numColours = colourRange.size();
	heatColours[heatDistances.begin()->first] = *colourRange.begin();
	if (maxDist > 1)
	{
		for (std::advance(distit, 1); distit != heatDistances.end(); distit++)
		{
			float distratio = (float)distit->second / (float)maxDist;
			int colourIndex = min(numColours-1, floor(numColours*distratio));
			heatColours[distit->first] = colourRange.at(colourIndex);
		}
	}

	unsigned long lastColour = heatDistances.rbegin()->first;
	if (heatColours.size() > 1)
		heatColours[lastColour] = *colourRange.rbegin();
	
	graph->heatmaplines->reset();

	//finally build a colours buffer using the heat/colour map entry for each edge weight
	vector <float> *lineVector = graph->heatmaplines->acquire_col_write();
	unsigned int edgeindex = 0;
	unsigned int edgeEnd = graph->get_mainlines()->get_renderedEdges();
	EDGELIST* edgelist = graph->get_protoGraph()->edgeLptr();

	COLSTRUCT debuggingUnfin;
	debuggingUnfin.a = 1;
	debuggingUnfin.b = 0;
	debuggingUnfin.g = 1;
	debuggingUnfin.r = 0;

	//draw the heatmap
	for (; edgeindex < edgeEnd; ++edgeindex)
	{
		edge_data *edge = protoGraph->get_edge(edgeindex);
		
		if (!edge) {
			cerr << "[rgat]WARNING: Heatmap2 edge skip"<<endl;
			continue;
		}

		COLSTRUCT *edgeColour = 0;
		//map<unsigned long, COLSTRUCT>::iterator foundHeatColour = heatColours.find(edge->weight);
		map<unsigned long, COLSTRUCT>::iterator foundHeatColour = heatColours.find(edge->chainedWeight);

		//this edge has a new value since we recalculated the heats, this finds the nearest
		if (foundHeatColour == heatColours.end())
		{
			//just make them green, not as big of an issue with new trace format
			edgeColour = &debuggingUnfin;
			/*
			unsigned long searchWeight = edge->weight;
			map<unsigned long, COLSTRUCT>::iterator previousHeatColour = foundHeatColour;
			for (foundHeatColour = heatColours.begin(); foundHeatColour != heatColours.end(); ++foundHeatColour)
			{
				if (foundHeatColour->first > searchWeight && previousHeatColour->first < searchWeight)
				{
					edgeColour = &foundHeatColour->second;
					break;
				}
				previousHeatColour = foundHeatColour;
			}
			if (foundHeatColour == heatColours.end())
				edgeColour = &heatColours.rbegin()->second;
			//record it so any others with this weight are found
			heatColours[searchWeight] = *edgeColour;
			*/
		}
		else
			edgeColour = &foundHeatColour->second;

		float edgeColArr[4] = { edgeColour->r, edgeColour->g, edgeColour->b, edgeColour->a};

		unsigned int vertIdx = 0;
		assert(edge->vertSize);
		for (; vertIdx < edge->vertSize; ++vertIdx)
			lineVector->insert(lineVector->end(), edgeColArr, end(edgeColArr));

		graph->heatmaplines->inc_edgesRendered();
		graph->heatmaplines->set_numVerts(graph->heatmaplines->get_numVerts() + vertIdx);
	}

	graph->heatmaplines->release_col_write();
	graph->needVBOReload_heatmap = true;
	
	return true;
}

//convert 0-255 rgb to 0-1
inline float fcol(int value)
{
	return (float)value / 255.0;
}

//allegro_color kept breaking here and driving me insane - hence own stupidly redundant implementation
COLSTRUCT *col_to_colstruct(ALLEGRO_COLOR *c)
{
	COLSTRUCT *cs = new COLSTRUCT;
	cs->r = c->r;
	cs->g = c->g;
	cs->b = c->b;
	cs->a = c->a;
	return cs;
}

//thread handler to build graph for each thread
//allows display in thumbnail style format
void heatmap_renderer::main_loop()
{
	alive = true;
	//add our heatmap colours to a vector for lookup in render thread
	for (int i = 0; i < 10; i++)
		colourRange.insert(colourRange.begin(), *col_to_colstruct(&clientState->config->heatmap.edgeFrequencyCol[i]));

	while ((!piddata || piddata->plottedGraphs.empty()) && !die)
		Sleep(100);
	Sleep(500);

	map<plotted_graph *, bool> finishedGraphs;
	while (!clientState->die)
	{
		obtainMutex(piddata->graphsListMutex, 1054);

		vector<plotted_graph *> graphlist;
		map <PID_TID, void *>::iterator graphIt = piddata->plottedGraphs.begin();
		for (; graphIt != piddata->plottedGraphs.end(); ++graphIt)
		{
			plotted_graph *g = (plotted_graph *)graphIt->second;
			if (g->increase_thread_references(333))
				graphlist.push_back(g);
		}
		dropMutex(piddata->graphsListMutex);

		//process terminated, all graphs fully rendered, now can head off to valhalla
		if (!piddata->is_running() && (finishedGraphs.size() == graphlist.size()))
		{
			vector<plotted_graph *>::iterator graphlistIt = graphlist.begin();
			for (;graphlistIt != graphlist.end(); graphlistIt++)
			{
				((plotted_graph *)*graphlistIt)->decrease_thread_references(343);
			}
			break;
		}

		vector<plotted_graph *>::iterator graphlistIt = graphlist.begin();
		while (graphlistIt != graphlist.end() && !die)
		{
			plotted_graph *graph = *graphlistIt++;
			proto_graph * protoGraphEnd = graph->get_protoGraph();
			//always rerender an active graph (edge executions may have increased without adding new edges)
			//render saved graphs if there are new edges
			if (protoGraphEnd->active || protoGraphEnd->get_num_edges() > graph->heatmaplines->get_renderedEdges())
			{
				render_graph_heatmap(graph, false);
			}
			else //last mop-up rendering of a recently finished graph
				if (!finishedGraphs[graph])
				{
					finishedGraphs[graph] = true;
					render_graph_heatmap(graph, true);
				}
			Sleep(20); //pause between graphs so other things don't struggle for mutex time
		}
		
		for (graphlistIt = graphlist.begin(); graphlistIt != graphlist.end(); graphlistIt++)
			((plotted_graph *)*graphlistIt)->decrease_thread_references(343);
		graphlist.clear();

		int waitForNextIt = 0;
		while (waitForNextIt < updateDelayMS && !die)
		{
			Sleep(50);
			waitForNextIt += 50;
		}
	}

	alive = false;
}

