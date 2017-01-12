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
Header for the preview pane, with previews of each graph in a scrolling vertical bar
*/
#include <stdafx.h>
#include "GUIStructs.h"
#include "opengl_operations.h"
#include "traceMisc.h"
#include "GUIManagement.h"
#include "preview_pane.h"

void write_text(ALLEGRO_FONT* font, ALLEGRO_COLOR textcol, int x, int y, const char *label)
{
	al_draw_text(font, textcol, x, y, ALLEGRO_ALIGN_LEFT, label);
}

void write_tid_text(ALLEGRO_FONT* font, plotted_graph *graph, int x, int y)
{
	stringstream infotxt;
	ALLEGRO_COLOR textcol;
	proto_graph *graphData = graph->get_protoGraph();

	infotxt << "TID:" << graphData->get_TID();

	if (graphData->active)
	{
		textcol = al_col_green;
		infotxt << " (Active)";

		unsigned long backlog = graphData->get_backlog_total();
		if (backlog > 10)
			infotxt << " Q:" << backlog;

		unsigned long newIn = graphData->getBacklogIn();
		if (newIn)
			infotxt << " in:" << newIn;
	}
	else
	{
		textcol = al_col_white;
		infotxt << " (Finished)";
	}
	write_text(font, textcol, x+3, y+2, infotxt.str().c_str());
}


void uploadPreviewGraph(plotted_graph *previewgraph)
{
	GLuint *VBOs = previewgraph->previewVBOs;
	load_VBO(VBO_NODE_POS, VBOs, previewgraph->previewnodes->pos_size(), previewgraph->previewnodes->readonly_pos());
	load_VBO(VBO_NODE_COL, VBOs, previewgraph->previewnodes->col_size(), previewgraph->previewnodes->readonly_col());

	int posbufsize = previewgraph->previewlines->get_numVerts() * POSELEMS * sizeof(GLfloat);
	int linebufsize = previewgraph->previewlines->get_numVerts() * COLELEMS * sizeof(GLfloat);
	if (!posbufsize || !linebufsize) return;

	vector<float> *lineVector = 0;
	
	lineVector = previewgraph->previewlines->acquire_pos_read(25);
	if (previewgraph->previewlines->get_numVerts() == 0)
	{
		previewgraph->previewlines->release_pos_read();
		return;
	}
	assert(!lineVector->empty());
	load_VBO(VBO_LINE_POS, VBOs, posbufsize, &lineVector->at(0));
	previewgraph->previewlines->release_pos_read();

	lineVector = previewgraph->previewlines->acquire_col_read();
	assert(!lineVector->empty());
	load_VBO(VBO_LINE_COL, VBOs, linebufsize, &lineVector->at(0));
	previewgraph->previewlines->release_col_read();

	previewgraph->needVBOReload_preview = false;
}


void drawGraphBitmap(plotted_graph *previewgraph, VISSTATE *clientState)
{

	if (!previewgraph->previewBMP)
	{
		al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);
		previewgraph->previewBMP = al_create_bitmap(PREVIEW_GRAPH_WIDTH, PREVIEW_GRAPH_HEIGHT);
	}

	if (previewgraph->needVBOReload_preview)
		uploadPreviewGraph(previewgraph);

	ALLEGRO_BITMAP *previousBmp = al_get_target_bitmap();

	al_set_target_bitmap(previewgraph->previewBMP);
	ALLEGRO_COLOR preview_gcol;
	if (previewgraph->get_protoGraph()->active)
		preview_gcol = clientState->config->preview.activeHighlight;
	else
		preview_gcol = clientState->config->preview.inactiveHighlight;
	al_clear_to_color(preview_gcol);

	//draw white box around the preview we are looking at
	if (clientState->activeGraph == previewgraph)
	{
		glColor3f(1, 1, 1);
		glBegin(GL_LINE_LOOP);

		glVertex3f(1, 1, 0);
		glVertex3f(PREVIEW_GRAPH_WIDTH-1, 1, 0);
		glVertex3f(PREVIEW_GRAPH_WIDTH-1, PREVIEW_GRAPH_HEIGHT-1, 0);
		glVertex3f(1, PREVIEW_GRAPH_HEIGHT - 1, 0);
		glVertex3f(1, 1, 0);
		glEnd();

	}

	glPushMatrix();

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	double aspect = PREVIEW_GRAPH_WIDTH / PREVIEW_GRAPH_HEIGHT;
	gluPerspective(45, aspect, 1, 3000);
	glMatrixMode(GL_MODELVIEW);

	glTranslatef(0, -20, 0);
	glTranslatef(0, 0, -550);

	glRotatef(30, 1, 0, 0);

	glRotatef(clientState->previewYAngle, 0, 1, 0);
	glRotatef(90 / 1.0, 0, 1, 0);
	
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	array_render_points(VBO_NODE_POS, VBO_NODE_COL, previewgraph->previewVBOs, previewgraph->previewnodes->get_numVerts());
	array_render_lines(VBO_LINE_POS, VBO_LINE_COL, previewgraph->previewVBOs, previewgraph->previewlines->get_numVerts());
	glPopMatrix();
	
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);

	al_set_target_bitmap(previousBmp);
}


bool find_mouseover_thread(VISSTATE *clientState, int mousex, int mousey, PID_TID *PID, PID_TID* TID)
{
	if (mousex >= clientState->mainFrameSize.width && mousex <= clientState->displaySize.width)
	{
		map <PID_TID, NODEPAIR>::iterator graphPosIt = clientState->graphPositions.begin();
		for (; graphPosIt != clientState->graphPositions.end(); graphPosIt++)
		{
			const int graphTop = graphPosIt->first;
			if (mousey >= graphTop && mousey <= (graphTop + PREVIEW_GRAPH_HEIGHT))
			{
				*PID = graphPosIt->second.first;
				*TID = graphPosIt->second.second;
				return true;
			}
		}
	}

	*PID = -1;
	*TID = -1;
	return false;
}

void redrawPreviewGraphs(VISSTATE *clientState, map <PID_TID, NODEPAIR> *graphPositions)
{

	if (clientState->glob_piddata_map.empty() || !clientState->activeGraph)
		return;

	ALLEGRO_BITMAP *previousBmp = al_get_target_bitmap();
	bool spinGraphs = true;

	glPointSize(PREVIEW_POINT_SIZE);

	ALLEGRO_COLOR preview_bgcol = clientState->config->preview.background;
	int first = 0;

	if (!obtainMutex(clientState->pidMapMutex, 1051)) return;

	plotted_graph *previewGraph = 0;

	TraceVisGUI *widgets = (TraceVisGUI *)clientState->widgets;
	int graphy = -1*PREV_Y_MULTIPLIER*widgets->getScroll();
	int numGraphs = 0;
	int graphsHeight = 0; 
	const float spinPerFrame = clientState->config->preview.spinPerFrame;

	al_set_target_bitmap(clientState->previewPaneBMP);
	al_clear_to_color(preview_bgcol);

	glLoadIdentity();
	glPushMatrix();

	map<PID_TID, void *>::iterator threadit = clientState->activePid->plottedGraphs.begin();
	for (;threadit != clientState->activePid->plottedGraphs.end(); ++threadit)
	{
		previewGraph = (plotted_graph *)threadit->second;
	
		if (!previewGraph || !previewGraph->previewnodes->get_numVerts()) continue;

		if (!previewGraph->VBOsGenned) 
			previewGraph->gen_graph_VBOs();

		if (spinPerFrame || previewGraph->needVBOReload_preview)
			drawGraphBitmap(previewGraph, clientState);

		al_set_target_bitmap(clientState->previewPaneBMP);
		al_draw_bitmap(previewGraph->previewBMP, PREV_GRAPH_PADDING, graphy, 0);

		write_tid_text(clientState->standardFont, previewGraph, PREV_GRAPH_PADDING, graphy);

		PID_TID TID = threadit->first;
		clientState->graphPositions[50+graphy] = make_pair(clientState->activePid->PID, TID);
		graphy += PREV_Y_MULTIPLIER;
		graphsHeight += PREV_Y_MULTIPLIER;
		++numGraphs;

	}

	glPopMatrix();
	dropMutex(clientState->pidMapMutex);
	al_set_target_bitmap(previousBmp);

	int scrollDiff = graphsHeight - clientState->displaySize.height;

	if (scrollDiff > 0) 
		widgets->setScrollbarMax(numGraphs - (clientState->displaySize.height / PREV_Y_MULTIPLIER));

	if (spinPerFrame)
	{
		clientState->previewYAngle -= spinPerFrame;
		if (clientState->previewYAngle < -360) clientState->previewYAngle = 0;
	}
}
