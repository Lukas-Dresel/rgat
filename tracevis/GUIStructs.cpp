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
Client state functions
*/
#include "stdafx.h"
#include "GUIStructs.h"
#include "plotted_graph.h"
#include "rendering.h"
#include "preview_pane.h"
#include "GUIManagement.h"

void VISSTATE::set_activeGraph(void *graph)
{
	if (activeGraph)
	{
		plotted_graph *oldGraph = (plotted_graph *)activeGraph;
		oldGraph->decrease_thread_references();
	}
	((plotted_graph *)graph)->increase_thread_references();
	cout << "setting new activegraph " << std::hex << graph << endl;
	activeGraph = graph;
}

void VISSTATE::deleteOldGraphs()
{
	double timenow = al_get_time();

	vector <pair<void *, double>>::iterator graphIt =  deletionGraphsTimes.begin();
	while (graphIt != deletionGraphsTimes.end())
	{
		double deletionTime = graphIt->second + 5;
		if (timenow > deletionTime)
		{
			plotted_graph *deadGraph = (plotted_graph *)graphIt->first;
			delete deadGraph;
			graphIt = deletionGraphsTimes.erase(graphIt);
		}
		else
			graphIt++;
	}
}

void VISSTATE::irregularActions()
{
	deleteOldGraphs();
}

void VISSTATE::change_mode(eUIEventCode mode)
{
	switch (mode)
	{
	case EV_BTN_WIREFRAME:
		modes.wireframe = !modes.wireframe;
		break;

	case EV_BTN_CONDITION:

		modes.conditional = !modes.conditional;
		if (modes.conditional)
		{
			modes.nodes = true;
			modes.heatmap = false;
			backgroundColour = config->conditional.background;
		}
		else
			backgroundColour = config->mainBackground;

		break;

	case EV_BTN_HEATMAP:

		modes.heatmap = !modes.heatmap;
		modes.nodes = !modes.heatmap;
		if (modes.heatmap) modes.conditional = false;
		break;

	case EV_BTN_PREVIEW:
	{
		al_destroy_bitmap(mainGraphBMP);
		modes.preview = !modes.preview;

		TraceVisGUI *mywidgets = (TraceVisGUI *)widgets;
		if (modes.preview)
		{
			mywidgets->setScrollbarVisible(true);
			mainGraphBMP = al_create_bitmap(mainFrameSize.width, mainFrameSize.height);
		}
		else
		{
			mywidgets->setScrollbarVisible(false);
			mainGraphBMP = al_create_bitmap(displaySize.width, mainFrameSize.height);
		}

		break;
	}
	case EV_BTN_DIFF:
		modes.heatmap = false;
		modes.conditional = false;
		break;

	case EV_BTN_NODES:
		modes.nodes = !modes.nodes;
		break;

	case EV_BTN_EDGES:
		modes.edges = !modes.edges;
		break;
	}
}


void VISSTATE::draw_display_diff(ALLEGRO_FONT *font, void **diffRenderer)
{
	diff_plotter *diffRendererPtr = *(diff_plotter **)diffRenderer;

	if (modes.diffView == eDiffRendered) //diff graph built, display it
	{
		plotted_graph *graph1 = diffRendererPtr->get_graph(1);
		proto_graph *protoGraph1 = graph1->get_protoGraph();

		node_data *diffnode = 0;
		if (diffRendererPtr->wasDivergenceFound())
			diffnode = protoGraph1->safe_get_node(diffRendererPtr->get_diff_node());
		
		display_graph_diff(this, diffRendererPtr, diffnode);
	}

	else if (modes.diffView == eDiffSelected)//diff button clicked, build the graph first
	{
		change_mode(EV_BTN_DIFF);

		modes.diffView = eDiffRendered;
		TraceVisGUI *mywidgets = (TraceVisGUI *)widgets;
		mywidgets->showHideDiffFrame();

		plotted_graph *graph1 = mywidgets->diffWindow->get_graph(1);
		plotted_graph *graph2 = mywidgets->diffWindow->get_graph(2);

		diff_plotter **diffRenderPtrPtr = (diff_plotter **)diffRenderer;
		*diffRenderPtrPtr = new diff_plotter(graph1, graph2, this);
		((diff_plotter*)*diffRenderPtrPtr)->render();
		diffRendererPtr = *(diff_plotter **)diffRenderer;
	}

	diffRendererPtr->display_diff_summary(20, LAYOUT_ICONS_Y + LAYOUT_ICONS_H, font, this);
}


/*
performs actions that need to be done quite often, but not every frame
this includes checking the locations of the screen edge on the sphere and
drawing new highlights for things that match the active filter
*/
void VISSTATE::performIrregularActions()
{


	plotted_graph * graph = (plotted_graph *)activeGraph;
	graph->irregularActions(this);

	HIGHLIGHT_DATA *highlightData = &graph->highlightData;
	if (highlightData->highlightState && graph->get_protoGraph()->active)
	{
		((TraceVisGUI *)widgets)->highlightWindow->updateHighlightNodes(highlightData, graph->get_protoGraph(), activePid);
	}
}


void VISSTATE::displayActiveGraph()
{
	plotted_graph *thisActiveGraph = (plotted_graph *)activeGraph;
	if (!activeGraph) return;

	al_set_target_bitmap(mainGraphBMP);
	frame_gl_setup(this);

	al_clear_to_color(backgroundColour);

	//set to true if displaying the colour picking sphere
	if (!al_is_event_queue_empty(low_frequency_timer_queue))
	{
		al_flush_event_queue(low_frequency_timer_queue);
		performIrregularActions();
	}

	if (modes.diffView == eDiffInactive)
		thisActiveGraph->performMainGraphDrawing(this);
	else
		draw_display_diff(PIDFont, &diffRenderer);

	frame_gl_teardown();

	if (animFinished)
	{
		animFinished = false;
		((TraceVisGUI*)widgets)->controlWindow->notifyAnimFinished();
	}

	al_set_target_backbuffer(maindisplay);
	if (modes.preview)
	{
		if (previewRenderFrame++ % (TARGET_FPS / config->preview.FPS))
		{
			//update and draw preview graphs onto the previewpane bitmap
			redrawPreviewGraphs(this, &graphPositions);
			previewRenderFrame = 0;
		}
		//draw previews on the screen
		al_draw_bitmap(previewPaneBMP, mainFrameSize.width, MAIN_FRAME_Y, 0);
	}
	//draw the main big graph bitmap on the screen
	al_draw_bitmap(mainGraphBMP, 0, 0, 0);

	display_activeGraph_summary(20, 10, PIDFont, this);
}

void VISSTATE::setInstructionFontSize(int ptSize)
{
	if (instructionFont)
		al_destroy_font(instructionFont);

	instructionFont = al_load_ttf_font(instructionFontpath.c_str(), ptSize, 0);
}

bool VISSTATE::mouseInDialog(int mousex, int mousey)
{
	vector<agui::Frame *>::iterator frameIt = openFrames.begin();
	for (; frameIt != openFrames.end(); frameIt++)
	{
		agui::Frame * frame = *frameIt;
		agui::Rectangle framePos = frame->getAbsoluteRectangle();
		if (mousex >= framePos.getLeft() && mousex <= framePos.getRight() && mousey >= framePos.getTop() && mousey <= framePos.getBottom())
			return true;
	}
	return false;
}

void VISSTATE::closeFrame(agui::Frame *frame)
{
	if (!frame->isVisible()) return;
	frame->setVisibility(false);
	openFrames.erase(std::remove(openFrames.begin(), openFrames.end(), frame), openFrames.end());
}

void VISSTATE::openFrame(agui::Frame *frame)
{
	if (frame->isVisible()) return;
	frame->setVisibility(true);
	openFrames.push_back(frame);
}