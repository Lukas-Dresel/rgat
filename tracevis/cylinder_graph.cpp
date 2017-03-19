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
Creates a sphere layout for a plotted graph
*/

#include "stdafx.h"
#include "cylinder_graph.h"
#include "rendering.h"


//A: Longitude. How many units along the side of the sphere a node is placed
//B: Latitude. How many units up or down the side of the sphere a node is placed

#define B_PX_OFFSET_FROM_TOP 35
#define PIX_PER_B_COORD -120
#define PIX_PER_B_MOD -30



#define JUMPA 3
#define JUMPB 3
#define JUMPA_CLASH 15
#define CALLB 3

//how to adjust placement if it jumps to a prexisting node (eg: if caller has called multiple)
#define CALLA_CLASH 12
#define CALLB_CLASH -12

//placement of external nodes, relative to the first caller
#define EXTERNA -0.5
#define EXTERNB 0.5

//controls placement of the node after a return
#define RETURNA_OFFSET -7
#define RETURNB_OFFSET 5


//performs an action (call,jump,etc) from lastNode, places new position in positionStruct
//this is the function that determines how the graph is laid out
void cylinder_graph::positionVert(void *positionStruct, node_data *n, PLOT_TRACK *lastNode)
{

	SPHERECOORD *oldPosition = get_node_coord(lastNode->lastVertID);
	if (!oldPosition)
	{
		cerr << "Waiting for node " << lastNode->lastVertID;
		int waitPeriod = 5;
		int iterations = 1;
		do
		{
			Sleep(waitPeriod);
			waitPeriod += (150 * iterations++);
			oldPosition = get_node_coord(lastNode->lastVertID);
		} while (!oldPosition);
	}

	int a = oldPosition->a;
	int b = oldPosition->b;
	int bMod = oldPosition->bMod;
	int clash = 0;

	SPHERECOORD *position = (SPHERECOORD *)positionStruct;
	if (n->external)
	{
		node_data *lastNodeData = internalProtoGraph->safe_get_node(lastNode->lastVertID);
		position->a = a + EXTERNA - 1 * lastNodeData->childexterns;
		position->b = b + EXTERNB + lastNodeData->childexterns;
		position->bMod = bMod;
		return;
	}

	switch (lastNode->lastVertType)
	{

		//small vertical distance between instructions in a basic block	
	case eNodeNonFlow:
	{
		bMod += 1;
		break;
	}

	case eNodeJump://long diagonal separation to show distinct basic blocks
	{
		//check if this is a conditional which fell through (ie: sequential)
		node_data *lastNodeData = internalProtoGraph->safe_get_node(lastNode->lastVertID);
		if (lastNodeData->conditional && n->address == lastNodeData->ins->condDropAddress)
		{
			bMod += 1;
			break;
		}
		//notice lack of break
	}

	case eNodeException:
	{
		a += JUMPA;
		b += JUMPB;

		while (usedCoords.find(make_pair(a, b)) != usedCoords.end())
		{
			a += JUMPA_CLASH;
			++clash;
		}

		//if (clash > 15)
		//	cerr << "[rgat]WARNING: Dense Graph Clash (jump) - " << clash << " attempts" << endl;
		break;
	}

	//long purple line to show possible distinct functional blocks of the program
	case eNodeCall:
	{
		//note: b sometimes huge after this?
		b += CALLB;

		while (usedCoords.find(make_pair(a, b)) != usedCoords.end())
		{
			a += CALLA_CLASH;
			b += CALLB_CLASH;
			++clash;
		}

		if (clash)
		{
			a += CALLA_CLASH;
			//if (clash > 15)
			//	cerr << "[rgat]WARNING: Dense Graph Clash (call) - " << clash <<" attempts"<<endl;
		}
		break;
	}

	case eNodeReturn:
		//previous externs handled same as previous returns
	case eNodeExternal:
	{
		//returning to address in call stack?
		int result = -1;

		vector<pair<MEM_ADDRESS, unsigned int>>::iterator stackIt;
		for (stackIt = callStack->begin(); stackIt != callStack->end(); ++stackIt)
			if (stackIt->first == n->address)
			{
				result = stackIt->second;
				break;
			}

		//if so, position next node near caller
		if (result != -1)
		{
			SPHERECOORD *caller = get_node_coord(result);
			assert(caller);
			a = caller->a + RETURNA_OFFSET;
			b = caller->b + RETURNB_OFFSET;
			bMod = caller->bMod;

			//may not have returned to the last item in the callstack
			//delete everything inbetween
			callStack->resize(stackIt - callStack->begin());
		}
		else
		{
			a += RETURNA_OFFSET;
			b += RETURNB_OFFSET;
		}

		while (usedCoords.find(make_pair(a, b)) != usedCoords.end())
		{
			a += JUMPA_CLASH;
			b += 1;
			++clash;
		}

		//if (clash > 15)
		//	cerr << "[rgat]WARNING: Dense Graph Clash (extern) - " << clash << " attempts" << endl;
		break;
	}

	default:
		if (lastNode->lastVertType != eFIRST_IN_THREAD)
			cerr << "[rgat]ERROR: Unknown Last instruction type " << lastNode->lastVertType << endl;
		break;
	}

	cout << "node " << n->index << " pos: a=" << a << ", b=" << b << ", bMod=" << bMod << endl;
	position->a = a;
	position->b = b;
	position->bMod = bMod;
}

void cylinder_graph::initialise()
{
	layout = eCylinderLayout;
};

void cylinder_graph::initialiseDefaultDimensions()
{
	wireframeSupported = true;
	preview_scalefactors->AEDGESEP = 1;
	preview_scalefactors->BEDGESEP = 1;
	preview_scalefactors->size = 400;
	preview_scalefactors->baseSize = 400;

	main_scalefactors->size = 7000;
	main_scalefactors->baseSize = 7000;

	defaultViewShift = make_pair(135, -25);
	defaultZoom = 80000;
}


SPHERECOORD * cylinder_graph::get_node_coord(NODEINDEX idx)
{
	if (idx < node_coords.size())
	{
		SPHERECOORD *result;
		acquire_nodecoord_read();
		result = &node_coords.at(idx);
		release_nodecoord_read();
		return result;
	}
	return 0;
}


bool cylinder_graph::a_coord_on_screen(int a, float hedgesep)
{
	//TODO after cylinder implementation! it does not need scaling, so this should be a straightforward formula
	//
	return true;
}

//take longitude a, latitude b, output coord in space
//diamModifier allows specifying different sphere sizes
void cylinder_graph::cylinderCoord(SPHERECOORD *sc, FCOORD *c, GRAPH_SCALE *dimensions, float diamModifier)
{
	return cylinderCoord(sc->a, sc->b, sc->bMod, c, dimensions, diamModifier);
}

void cylinder_graph::cylinderCoord(float a, float b, int bmod, FCOORD *c, GRAPH_SCALE *dimensions, float diamModifier)
{

	float fa = a;
	float fb = 0;
	fb += B_PX_OFFSET_FROM_TOP; //offset start down on cylinder
	fb += b * PIX_PER_B_COORD;
	fb += bmod * PIX_PER_B_MOD;

			   //float sinb = sin((b*M_PI) / 180);
	float r = (dimensions->size + diamModifier);// +0.1 to make sure we are above lines

	c->x = r *  cos((a*M_PI) / 180);
	c->y = fb;
	c->z = r *  sin((a*M_PI) / 180);
}

//take coord in space, convert back to a/b
void cylinder_graph::cylinderAB(FCOORD *c, float *a, float *b, GRAPH_SCALE *mults)
{
	float r = mults->size;
	//float acosb = acos(c->y / (mults->size + 0.099));
	float tb = c->y;  //acos is a bit imprecise / wrong...

	float ta = DEGREESMUL * (asin((c->z / r + 0.1)));
	tb -= B_PX_OFFSET_FROM_TOP;
	*a = ta;
	*b = (tb / PIX_PER_B_COORD);
}

//double version
void cylinder_graph::cylinderAB(DCOORD *c, float *a, float *b, GRAPH_SCALE *mults)
{
	FCOORD FFF;
	FFF.x = c->x;
	FFF.y = c->y;
	FFF.z = c->z;
	cylinderAB(&FFF, a, b, mults);
}

//connect two nodes with an edge of automatic number of vertices
int cylinder_graph::drawCurve(GRAPH_DISPLAY_DATA *linedata, FCOORD *startC, FCOORD *endC,
	ALLEGRO_COLOR *colour, int edgeType, GRAPH_SCALE *dimensions, int *arraypos)
{
	float r, b, g, a;
	r = colour->r;
	b = colour->b;
	g = colour->g;
	a = colour->a;

	//describe the normal
	FCOORD middleC;
	midpoint(startC, endC, &middleC);
	float eLen = linedist(startC, endC);

	FCOORD bezierC;
	int curvePoints;

	switch (edgeType)
	{
	case eEdgeNew:
	{
		//todo: make this number much smaller for previews
		curvePoints = eLen < 80 ? 1 : LONGCURVEPTS;
		bezierC = middleC;
		break;
	}

	case eEdgeOld:
	case eEdgeReturn:
	{
		curvePoints = LONGCURVEPTS;

		if (eLen < 2)
			bezierC = middleC;
		else
		{
			float oldMidA, oldMidB;
			FCOORD bezierC2;
			cylinderAB(&middleC, &oldMidA, &oldMidB, dimensions);
			cylinderCoord(oldMidA, oldMidB, 0, &bezierC, dimensions, -(eLen / 2));

			//i dont know why this problem happens or why this fixes it
			if ((bezierC.x > 0) && (startC->x < 0 && endC->x < 0))
				bezierC.x = -bezierC.x;
		}
		break;
	}

	case eEdgeCall:
	case eEdgeLib:
	case eEdgeException:
	{
		curvePoints = LONGCURVEPTS;
		bezierC = middleC;
		break;
	}

	default:
		cerr << "[rgat]Error: Drawcurve unknown edgeType " << edgeType << endl;
		return 0;
	}

	switch (curvePoints)
	{
	case LONGCURVEPTS:
	{
		int vertsdrawn = drawLongCurvePoints(&bezierC, startC, endC, colour, edgeType, linedata, curvePoints, arraypos);
		return vertsdrawn;
	}

	case 1:
		drawShortLinePoints(startC, endC, colour, linedata, arraypos);
		return 2;

	default:
		cerr << "[rgat]Error: Drawcurve unknown curvePoints " << curvePoints << endl;
	}

	return curvePoints;
}

void cylinder_graph::orient_to_user_view(int xshift, int yshift, long zoom)
{
	glTranslatef(0, 0, -zoom);
	glTranslatef(0, yshift * 60, 0);
	glRotatef(-xshift, 0, 1, 0);
}

//function names as they are executed
void cylinder_graph::write_rising_externs(ALLEGRO_FONT *font, bool nearOnly, int height, PROJECTDATA *pd)
{
	DCOORD nodepos;

	vector <pair<NODEINDEX, EXTTEXT>> displayNodeList;

	//make labels rise up screen, delete those that reach top
	obtainMutex(internalProtoGraph->externGuardMutex, 7676);
	map <NODEINDEX, EXTTEXT>::iterator activeExternIt = activeExternTimes.begin();
	for (; activeExternIt != activeExternTimes.end(); ++activeExternIt)
	{
		EXTTEXT *extxt = &activeExternIt->second;

		if (extxt->framesRemaining != KEEP_BRIGHT)
		{
			extxt->yOffset += EXTERN_FLOAT_RATE;

			if (extxt->framesRemaining-- == 0)
			{
				activeExternIt = activeExternTimes.erase(activeExternIt);
				if (activeExternIt == activeExternTimes.end())
					break;
				else
					continue;
			}
		}
		displayNodeList.push_back(make_pair(activeExternIt->first, activeExternIt->second));;
	}
	dropMutex(internalProtoGraph->externGuardMutex);

	vector <pair<NODEINDEX, EXTTEXT>>::iterator displayNodeListIt = displayNodeList.begin();

	for (; displayNodeListIt != displayNodeList.end(); ++displayNodeListIt)
	{
		internalProtoGraph->getNodeReadLock();
		SPHERECOORD *coord = get_node_coord(displayNodeListIt->first);
		internalProtoGraph->dropNodeReadLock();

		EXTTEXT *extxt = &displayNodeListIt->second;

		if (nearOnly && !a_coord_on_screen(coord->a, 1))
			continue;

		if (!get_screen_pos(displayNodeListIt->first, mainnodesdata, pd, &nodepos))
			continue;

		al_draw_text(font, al_col_green, nodepos.x, height - nodepos.y - extxt->yOffset,
			0, extxt->displayString.c_str());
	}

}

void cylinder_graph::draw_wireframe()
{
	glBindBuffer(GL_ARRAY_BUFFER, wireframeVBOs[VBO_SPHERE_POS]);
	glVertexPointer(POSELEMS, GL_FLOAT, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, wireframeVBOs[VBO_SPHERE_COL]);
	glColorPointer(COLELEMS, GL_FLOAT, 0, 0);

	glMultiDrawArrays(GL_LINE_LOOP, wireframeStarts, wireframeSizes, WIREFRAMELOOPS);
}


void cylinder_graph::gen_wireframe_buffers()
{
	glGenBuffers(2, wireframeVBOs);

	//wireframe drawn using glMultiDrawArrays which takes a list of vert starts/sizes
	wireframeStarts = (GLint *)malloc(WIREFRAMELOOPS * sizeof(GLint));
	wireframeSizes = (GLint *)malloc(WIREFRAMELOOPS * sizeof(GLint));
	for (int i = 0; i < WIREFRAMELOOPS; ++i)
	{
		wireframeStarts[i] = i*WF_POINTSPERLINE;
		wireframeSizes[i] = WF_POINTSPERLINE;
	}
}

//reads the list of nodes/edges, creates opengl vertex/colour data
//resizes when it wraps too far around the sphere (lower than lowB, farther than farA)
void cylinder_graph::render_static_graph(VISSTATE *clientState)
{
	bool doResize = false;
	if (doResize) previewNeedsResize = true;

	if (doResize || vertResizeIndex > 0)
	{
		zoomLevel = main_scalefactors->size;
		needVBOReload_main = true;

		if (wireframe_data)
			remakeWireframe = true;
	}

	int drawCount = render_new_edges(doResize);
	if (drawCount)
		needVBOReload_main = true;

	redraw_anim_edges();
}

void cylinder_graph::maintain_draw_wireframe(VISSTATE *clientState)
{
	if (!wireframeBuffersCreated)
	{
		wireframeBuffersCreated = true;
		gen_wireframe_buffers();
	}

	if (remakeWireframe)
	{
		delete wireframe_data;
		wireframe_data = 0;
		remakeWireframe = false;
	}

	if (!wireframe_data)
	{
		plot_wireframe(clientState);
	}

	draw_wireframe();
}

//must be called by main opengl context thread
void cylinder_graph::plot_wireframe(VISSTATE *clientState)
{
	cout << "plot wif called" << endl;
	wireframe_data = new GRAPH_DISPLAY_DATA(WFCOLBUFSIZE * 2);
	ALLEGRO_COLOR *wireframe_col = &clientState->config->wireframe.edgeColor;
	float cols[4] = { wireframe_col->r , wireframe_col->g, wireframe_col->b, wireframe_col->a };

	int ii, pp;
	long diam = main_scalefactors->size;
	const int points = WF_POINTSPERLINE;

	int lineDivisions = (int)(360 / WIREFRAMELOOPS);

	vector <float> *vpos = wireframe_data->acquire_pos_write(234);
	vector <float> *vcol = wireframe_data->acquire_col_write();
	//horizontal lines
	for (int rowY = 0; rowY < 20; rowY++)
	{
		for (pp = 0; pp < WF_POINTSPERLINE; ++pp) 
		{

			float angle = (2 * M_PI * pp) / WF_POINTSPERLINE;
			vpos->push_back(diam * cos(angle)); //x
			vpos->push_back(-rowY * CYLINDER_PIXELS_PER_ROW); //y
			vpos->push_back(diam * sin(angle)); //z

			vcol->insert(vcol->end(), cols, end(cols));
		}
	}

	/*
	for (int horizLine = 0; horizLine < WIREFRAMELOOPS; ii++)
	{

		float degs2 = (ii*M_PI) / 180;
		for (pp = 0; pp < points; ++pp) 
		{

			float angle = (2 * M_PI * pp) / points;
			float cosangle = cos(angle);
			vpos->push_back(diam * cosangle * cos(degs2));
			vpos->push_back(diam * sin(angle));
			vpos->push_back(diam * cosangle * sin(degs2));

			vcol->insert(vcol->end(), cols, end(cols));
		}
	}*/

	load_VBO(VBO_SPHERE_POS, wireframeVBOs, WFPOSBUFSIZE, &vpos->at(0));
	load_VBO(VBO_SPHERE_COL, wireframeVBOs, WFCOLBUFSIZE, &vcol->at(0));
	wireframe_data->release_pos_write();
	wireframe_data->release_col_write();
}

//draws a line from the center of the sphere to nodepos. adds lengthModifier to the end
void cylinder_graph::drawHighlight(NODEINDEX nodeIndex, GRAPH_SCALE *scale, ALLEGRO_COLOR *colour, int lengthModifier)
{
	FCOORD nodeCoordxyz;
	SPHERECOORD *nodeCoordSphere = get_node_coord(nodeIndex);
	if (!nodeCoordSphere) return;

	cylinderCoord(nodeCoordSphere, &nodeCoordxyz, scale, lengthModifier);
	drawHighlightLine(nodeCoordxyz, colour);
}

//draws a line from the center of the sphere to nodepos. adds lengthModifier to the end
void cylinder_graph::drawHighlight(void * nodeCoord, GRAPH_SCALE *scale, ALLEGRO_COLOR *colour, int lengthModifier)
{
	FCOORD nodeCoordxyz;
	if (!nodeCoord) return;

	cylinderCoord((SPHERECOORD *)nodeCoord,  &nodeCoordxyz, scale, lengthModifier);
	drawHighlightLine(nodeCoordxyz, colour);
}

//take the a/b/bmod coords, convert to opengl coordinates based on supplied sphere multipliers/size
FCOORD cylinder_graph::nodeIndexToXYZ(NODEINDEX index, GRAPH_SCALE *dimensions, float diamModifier)
{
	SPHERECOORD *nodeCoordSphere = get_node_coord(index);

	FCOORD result;
	cylinderCoord(nodeCoordSphere, &result, dimensions, diamModifier);
	return result;
}


//IMPORTANT: Must have edge reader lock to call this
bool cylinder_graph::render_edge(NODEPAIR ePair, GRAPH_DISPLAY_DATA *edgedata,
	ALLEGRO_COLOR *forceColour, bool preview, bool noUpdate)
{

	unsigned long nodeCoordQty = node_coords.size();
	if (ePair.second >= nodeCoordQty || ePair.first >= nodeCoordQty)
		return false;

	edge_data *e = &internalProtoGraph->edgeDict.at(ePair);

	GRAPH_SCALE *scaling;
	if (preview)
		scaling = preview_scalefactors;
	else
		scaling = main_scalefactors;

	FCOORD srcc = nodeIndexToXYZ(ePair.first, scaling, 0);
	FCOORD targc = nodeIndexToXYZ(ePair.second, scaling, 0);

	int arraypos = 0;
	ALLEGRO_COLOR *edgeColour;
	if (forceColour) edgeColour = forceColour;
	else
		edgeColour = &graphColours->at(e->edgeClass);

	int vertsDrawn = drawCurve(edgedata, &srcc, &targc,
		edgeColour, e->edgeClass, scaling, &arraypos);

	//previews, diffs, etc where we don't want to affect the original edges
	if (!noUpdate && !preview)
	{
		e->vertSize = vertsDrawn;
		e->arraypos = arraypos;
	}
	return true;
}


//converts a single node into node vertex data
int cylinder_graph::add_node(node_data *n, PLOT_TRACK *lastNode, GRAPH_DISPLAY_DATA *vertdata, GRAPH_DISPLAY_DATA *animvertdata,
	GRAPH_SCALE *dimensions)
{
	//printf("in add node! node %d\n", n->index);
	SPHERECOORD * spherecoord;
	if (n->index >= node_coords.size())
	{

		SPHERECOORD tempPos;
		if (node_coords.empty())
		{
			assert(n->index == 0);
			tempPos = { 0,0,0 };
			spherecoord = &tempPos;

			acquire_nodecoord_write();
			node_coords.push_back(tempPos);
			release_nodecoord_write();
		}
		else
		{
			positionVert(&tempPos, n, lastNode);
			spherecoord = &tempPos;

			acquire_nodecoord_write();
			node_coords.push_back(tempPos);
			release_nodecoord_write();
		}


		updateStats(tempPos.a, tempPos.b, tempPos.bMod);
		usedCoords.emplace(make_pair(make_pair(tempPos.a, tempPos.b), true));
	}
	else
		spherecoord = get_node_coord(n->index);

	FCOORD screenc;

	cylinderCoord(spherecoord, &screenc, dimensions, 0);

	vector<GLfloat> *mainNpos = vertdata->acquire_pos_write(677);
	vector<GLfloat> *mainNcol = vertdata->acquire_col_write();

	mainNpos->push_back(screenc.x);
	mainNpos->push_back(screenc.y);
	mainNpos->push_back(screenc.z);

	ALLEGRO_COLOR *active_col = 0;
	if (n->external)
		lastNode->lastVertType = eNodeExternal;
	else
	{
		switch (n->ins->itype)
		{
		case eInsUndefined:
			lastNode->lastVertType = n->conditional ? eNodeJump : eNodeNonFlow;
			break;

		case eInsJump:
			lastNode->lastVertType = eNodeJump;
			break;

		case eInsReturn:
			lastNode->lastVertType = eNodeReturn;
			break;

		case eInsCall:
		{
			lastNode->lastVertType = eNodeCall;
			//if code arrives to next instruction after a return then arrange as a function
			MEM_ADDRESS nextAddress = n->ins->address + n->ins->numbytes;
			callStack->push_back(make_pair(nextAddress, lastNode->lastVertID));
			break;
		}
		default:
			cerr << "[rgat]Error: add_node unknown itype " << n->ins->itype << endl;
			assert(0);
		}
	}

	active_col = &graphColours->at(lastNode->lastVertType);
	lastNode->lastVertID = n->index;

	mainNcol->push_back(active_col->r);
	mainNcol->push_back(active_col->g);
	mainNcol->push_back(active_col->b);
	mainNcol->push_back(1);

	vertdata->set_numVerts(vertdata->get_numVerts() + 1);

	vertdata->release_col_write();
	vertdata->release_pos_write();

	//place node on the animated version of the graph
	if (!vertdata->isPreview())
	{

		vector<GLfloat> *animNcol = animvertdata->acquire_col_write();

		animNcol->push_back(active_col->r);
		animNcol->push_back(active_col->g);
		animNcol->push_back(active_col->b);
		animNcol->push_back(0);

		animvertdata->set_numVerts(vertdata->get_numVerts() + 1);
		animvertdata->release_col_write();
	}

	return 1;
}

void cylinder_graph::performMainGraphDrawing(VISSTATE *clientState)
{
	if (get_pid() != clientState->activePid->PID) return;

	if (clientState->modes.wireframe)
		maintain_draw_wireframe(clientState);

	//add any new logged calls to the call log window
	if (clientState->textlog && clientState->logSize < internalProtoGraph->loggedCalls.size())
		clientState->logSize = internalProtoGraph->fill_extern_log(clientState->textlog, clientState->logSize);

	//line marking last instruction
	//<there may be a need to do something different depending on currentUnchainedBlocks.empty() or not>
	drawHighlight(lastAnimatedNode, main_scalefactors, &clientState->config->activityLineColour, 0);

	//highlight lines
	if (highlightData.highlightState)
		display_highlight_lines(&highlightData.highlightNodes,
			&clientState->config->highlightColour, clientState->config->highlightProtrusion);

	if (clientState->modes.heatmap)
	{
		display_big_heatmap(clientState);
		return;
	}

	if (clientState->modes.conditional)
	{
		display_big_conditional(clientState);
		return;
	}

	PROJECTDATA pd;
	gather_projection_data(&pd);
	display_graph(clientState, &pd);
	write_rising_externs(clientState->standardFont, clientState->modes.nearSide, clientState->mainFrameSize.height, &pd);
}

//standard animated or static display of the active graph
void cylinder_graph::display_graph(VISSTATE *clientState, PROJECTDATA *pd)
{
	if (clientState->modes.animation)
		display_active(clientState->modes.nodes, clientState->modes.edges);
	else
		display_static(clientState->modes.nodes, clientState->modes.edges);

	float zmul = zoomFactor(clientState->cameraZoomlevel, main_scalefactors->size);

	if (zmul < INSTEXT_VISIBLE_ZOOMFACTOR && internalProtoGraph->get_num_nodes() > 2)
		draw_instruction_text(clientState, zmul, pd);

	//if zoomed in, show all extern/internal labels
	if (zmul < EXTERN_VISIBLE_ZOOM_FACTOR)
		show_symbol_labels(clientState, pd);
	else
	{	//show label of extern we are blocked on
		node_data *n = internalProtoGraph->safe_get_node(lastMainNode.lastVertID);
		if (n && n->external)
		{
			DCOORD screenCoord;
			if (!get_screen_pos(lastMainNode.lastVertID, get_mainnodes(), pd, &screenCoord)) return;
			if (is_on_screen(&screenCoord, clientState->mainFrameSize.width, clientState->mainFrameSize.height))
				draw_func_args(clientState, clientState->standardFont, screenCoord, n);
		}
	}
}

//returns the screen coordinate of a node if it is on the screen
bool cylinder_graph::get_visible_node_pos(NODEINDEX nidx, DCOORD *screenPos, SCREEN_QUERY_PTRS *screenInfo)
{
	VISSTATE *clientState = screenInfo->clientState;
	SPHERECOORD *nodeCoord = get_node_coord(nidx);
	if (!nodeCoord)
		return false; //usually happens with block interrupted by exception

					  /*
					  this check removes the bulk of the offscreen instructions with low performance penalty, including those
					  on screen but on the other side of the sphere
					  implementation is tainted by a horribly derived constant that sometimes rules out nodes on screen
					  bypass by turning instruction display to "always on"
					  */

	if (!screenInfo->show_all_always && !a_coord_on_screen(nodeCoord->a, 1))
		return false;

	DCOORD screenCoord;
	if (!get_screen_pos(nidx, screenInfo->mainverts, screenInfo->pd, &screenCoord)) return false; //in graph but not rendered
	if (screenCoord.x > clientState->mainFrameSize.width || screenCoord.x < -100) return false;
	if (screenCoord.y > clientState->mainFrameSize.height || screenCoord.y < -100) return false;

	*screenPos = screenCoord;
	return true;
}




//this fails if we are drawing a node that has been recorded on the graph but not rendered graphically
//takes a node index and returns the x/y on the screen
bool cylinder_graph::get_screen_pos(NODEINDEX nodeIndex, GRAPH_DISPLAY_DATA *vdata, PROJECTDATA *pd, DCOORD *screenPos)
{
	FCOORD graphPos;
	if (!vdata->get_coord(nodeIndex, &graphPos)) return false;

	gluProject(graphPos.x, graphPos.y, graphPos.z,
		pd->model_view, pd->projection, pd->viewport,
		&screenPos->x, &screenPos->y, &screenPos->z);
	return true;
}

