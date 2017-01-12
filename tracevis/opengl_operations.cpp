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
Most of the OpenGL functions are here
*/
#include "stdafx.h"

#include "rendering.h"
#include "plotted_graph.h"

//this call is a bit sensitive and will give odd results if called in the wrong place
void gather_projection_data(PROJECTDATA *pd) 
{
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glGetDoublev(GL_MODELVIEW_MATRIX, pd->model_view);
	glGetDoublev(GL_PROJECTION_MATRIX, pd->projection);
	glGetIntegerv(GL_VIEWPORT, pd->viewport);
}

void frame_gl_setup(VISSTATE* clientState)
{
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();

	glLoadIdentity();

	plotted_graph *activeGraph = (plotted_graph *)clientState->activeGraph;

	bool zoomedIn = false;
	if (activeGraph)
	{
		float zmul = zoomFactor(clientState->cameraZoomlevel, activeGraph->main_scalefactors->radius);
		if (zmul < INSTEXT_VISIBLE_ZOOMFACTOR)
			zoomedIn = true;
	
		if (zoomedIn || clientState->modes.nearSide)
			gluPerspective(45, clientState->mainFrameSize.width / clientState->mainFrameSize.height, 500,
				clientState->cameraZoomlevel);
		else
			gluPerspective(45, clientState->mainFrameSize.width / clientState->mainFrameSize.height, 500, 
				clientState->cameraZoomlevel + activeGraph->main_scalefactors->radius);
	}
	else
		gluPerspective(45, clientState->mainFrameSize.width / clientState->mainFrameSize.height, 500,
			clientState->cameraZoomlevel);


	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	rotate_to_user_view(clientState);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glEnable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glEnable(GL_ALPHA);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}



void frame_gl_teardown()
{
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glPopMatrix();
	glPopMatrix();
}

void load_VBO(int index, GLuint *VBOs, int bufsize, float *data)
{
	glBindBuffer(GL_ARRAY_BUFFER, VBOs[index]);
	glBufferData(GL_ARRAY_BUFFER, bufsize, data, GL_DYNAMIC_DRAW);
}

void load_edge_VBOS(GLuint *VBOs, GRAPH_DISPLAY_DATA *lines)
{
	int posbufsize = lines->get_numVerts() * POSELEMS * sizeof(GLfloat);
	load_VBO(VBO_LINE_POS, VBOs, posbufsize, lines->readonly_pos());

	int linebufsize = lines->get_numVerts() * COLELEMS * sizeof(GLfloat);
	load_VBO(VBO_LINE_COL, VBOs, linebufsize, lines->readonly_col());
}

void loadVBOs(GLuint *VBOs, GRAPH_DISPLAY_DATA *verts, GRAPH_DISPLAY_DATA *lines)
{
	load_VBO(VBO_NODE_POS, VBOs, verts->pos_size(), verts->readonly_pos());
	load_VBO(VBO_NODE_COL, VBOs, verts->col_size(), verts->readonly_col());
	load_edge_VBOS(VBOs, lines);
}

void array_render(int prim, int POSVBO, int COLVBO, GLuint *buffers, int quantity)
{
	glBindBuffer(GL_ARRAY_BUFFER, buffers[POSVBO]);
	glVertexPointer(POSELEMS, GL_FLOAT, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, buffers[COLVBO]);
	glColorPointer(COLELEMS, GL_FLOAT, 0, 0);

	//Check VBOs have been loaded if crashing here
	glDrawArrays(prim, 0, quantity);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

}

void initial_gl_setup(VISSTATE *clientState)
{
	glEnable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glEnable(GL_ALPHA);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);

	glMatrixMode(GL_MODELVIEW);
	glPointSize(DEFAULTPOINTSIZE);
	glClearColor(0, 0, 0, 1.0);
}

//draw a segmented sphere with row gradiented red, cols green
void plot_colourpick_sphere(VISSTATE *clientState)
{
	GRAPH_DISPLAY_DATA *spheredata = clientState->col_pick_sphere;
	if (spheredata)
		delete spheredata;

	spheredata = new GRAPH_DISPLAY_DATA(COL_SPHERE_BUFSIZE);
	clientState->col_pick_sphere = spheredata;

	int diam = ((plotted_graph *)clientState->activeGraph)->main_scalefactors->radius;
	int rowi, coli;
	float tlx, tlz, trx, topy, trz;
	float basey, brx, brz, blz, blx;
	int rowAngle = (int)(360 / BDIVISIONS);
	
	vector<GLfloat> *spherepos = spheredata->acquire_pos_write(23);
	vector<GLfloat> *spherecol = spheredata->acquire_col_write();
	for (rowi = 180; rowi >= 0; rowi -= rowAngle) 
	{
		float colb = (float)rowi / 180;
		float ringSizeTop, ringSizeBase, anglel, angler;
		for (coli = 0; coli < ADIVISIONS; ++coli) 
		{
			float cola = 1 - ((float)coli / ADIVISIONS);
			float iitop = rowi;
			float iibase = rowi + rowAngle;

			anglel = (2 * M_PI * coli) / ADIVISIONS;
			angler = (2 * M_PI * (coli + 1)) / ADIVISIONS;

			ringSizeTop = diam * sin((iitop*M_PI) / 180);
			topy = diam * cos((iitop*M_PI) / 180);
			tlx = ringSizeTop * cos(anglel);
			trx = ringSizeTop * cos(angler);
			tlz = ringSizeTop * sin(anglel);
			trz = ringSizeTop * sin(angler);

			ringSizeBase = diam * sin((iibase*M_PI) / 180);
			basey = diam * cos((iibase*M_PI) / 180);
			blx = ringSizeBase * cos(anglel);
			blz = ringSizeBase * sin(anglel);
			brx = ringSizeBase * cos(angler);
			brz = ringSizeBase * sin(angler);

			int i;
			for (i = 0; i < 4; ++i)
			{
				spherecol->push_back(colb);
				spherecol->push_back(cola);
				spherecol->push_back(0);
			}

			//draw a segment of the sphere clockwise
			spherepos->push_back(tlx);
			spherepos->push_back(topy);
			spherepos->push_back(tlz);

			spherepos->push_back(trx);
			spherepos->push_back(topy);
			spherepos->push_back(trz);

			spherepos->push_back(brx);
			spherepos->push_back(basey);
			spherepos->push_back(brz);

			spherepos->push_back(blx);
			spherepos->push_back(basey);
			spherepos->push_back(blz);
		}
	}

	load_VBO(VBO_SPHERE_POS, clientState->colSphereVBOs, COL_SPHERE_BUFSIZE, &spherepos->at(0));
	load_VBO(VBO_SPHERE_COL, clientState->colSphereVBOs, COL_SPHERE_BUFSIZE, &spherecol->at(0));
	spheredata->release_col_write();
	spheredata->release_pos_write();
}

void rotate_to_user_view(VISSTATE *clientState)
{
	glTranslatef(0, 0, -clientState->cameraZoomlevel);
	glRotatef(-clientState->yturn, 1, 0, 0);
	glRotatef(-clientState->xturn, 0, 1, 0);
}

//draw a colourful gradiented sphere on the screen
//read colours on edge so we can see where window is on sphere
//reset back to state before the call
//return colours in passed SCREEN_EDGE_PIX struct
//pass doclear false if you want to see it for debugging
void edge_picking_colours(VISSTATE *clientState, SCREEN_EDGE_PIX *TBRG, bool doClear)
{
	if (!clientState->col_pick_sphere)
		plot_colourpick_sphere(clientState);

	glPushMatrix();
	//make sure camera only sees the nearest side of the sphere
	gluPerspective(45, clientState->mainFrameSize.width / clientState->mainFrameSize.height, 50, 
		clientState->cameraZoomlevel);
	glLoadIdentity();

	rotate_to_user_view(clientState);

	glBindBuffer(GL_ARRAY_BUFFER, clientState->colSphereVBOs[0]);
	glVertexPointer(3, GL_FLOAT, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, clientState->colSphereVBOs[1]);
	glColorPointer(3, GL_FLOAT, 0, 0);
	glDrawArrays(GL_QUADS, 0, COL_SPHERE_VERTS);

	//no idea why this ajustment needed, found by trial and error
	int height = clientState->mainFrameSize.height - 20;
	int width = al_get_bitmap_width(clientState->mainGraphBMP);
	int halfheight = height / 2;
	int halfwidth = width / 2;

	GLfloat pixelRGB[3];
	glReadPixels(0, halfheight, 1, 1, GL_RGB, GL_FLOAT, pixelRGB);
	TBRG->leftgreen = pixelRGB[1];
	glReadPixels(width - 1, halfheight, 1, 1, GL_RGB, GL_FLOAT, pixelRGB);
	TBRG->rightgreen = pixelRGB[1];
	//not used yet
	glReadPixels(halfwidth, height - 1, 1, 1, GL_RGB, GL_FLOAT, pixelRGB);
	TBRG->topred = pixelRGB[0];
	glReadPixels(halfwidth, 3, 1, 1, GL_RGB, GL_FLOAT, pixelRGB);
	TBRG->bottomred = pixelRGB[0];
	glPopMatrix();

	if (doClear) //also need to call this function on every frame to see it
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void array_render_points(int POSVBO, int COLVBO, GLuint *buffers, int quantity) 
{
	array_render(GL_POINTS, POSVBO, COLVBO, buffers, quantity);
}

void array_render_lines(int POSVBO, int COLVBO, GLuint *buffers, int quantity) 
{
	array_render(GL_LINES, POSVBO, COLVBO, buffers, quantity);
}

void draw_wireframe(VISSTATE *clientState, GLint *starts, GLint *sizes)
{
	glBindBuffer(GL_ARRAY_BUFFER, clientState->wireframeVBOs[VBO_SPHERE_POS]);
	glVertexPointer(POSELEMS, GL_FLOAT, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, clientState->wireframeVBOs[VBO_SPHERE_COL]);
	glColorPointer(COLELEMS, GL_FLOAT, 0, 0);

	glMultiDrawArrays(GL_LINE_LOOP, starts, sizes, WIREFRAMELOOPS);
}

void drawHighlightLine(FCOORD lineEndPt, ALLEGRO_COLOR *colour) {
	glColor4f(colour->r, colour->g, colour->b, colour->a);
	glBegin(GL_LINES);
	glVertex3f(0, 0, 0);
	glVertex3f(lineEndPt.x, lineEndPt.y, lineEndPt.z);
	glEnd();
}
