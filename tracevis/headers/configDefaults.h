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
Defaults for using when a config file exist
*/
#pragma once
#include "GUIConstants.h"

#define HEATMAP_DELAY_MS 1000
#define HEAT_EDGE_TEXT_COL al_col_orange
//rarest edges(FREQ9) to most often executed edges(FREQ0)
#define HEAT_EDGE_COL_FREQ9 al_map_rgba(0, 0, 255, 100)
#define HEAT_EDGE_COL_FREQ8 al_map_rgba(105, 0, 255, 160)
#define HEAT_EDGE_COL_FREQ7 al_map_rgba(182, 0, 255, 190)
#define HEAT_EDGE_COL_FREQ6 al_map_rgba(255, 0, 255, 230)
#define HEAT_EDGE_COL_FREQ5 al_map_rgba(255, 58, 0, 255)
#define HEAT_EDGE_COL_FREQ4 al_map_rgba(255, 93, 0, 255)
#define HEAT_EDGE_COL_FREQ3 al_map_rgba(255, 124, 0, 255)
#define HEAT_EDGE_COL_FREQ2 al_map_rgba(255, 163, 0, 255)
#define HEAT_EDGE_COL_FREQ1 al_map_rgba(255, 182, 0, 255)
#define HEAT_EDGE_COL_FREQ0 al_map_rgba(255, 228, 167, 255)


#define CONDITIONAL_DELAY_MS 1000
#define CONDITIONAL_edgeColor al_map_rgba(60, 60, 60, 255)
#define CONDITIONAL_background al_map_rgba(180, 180, 180, 255)
#define CONDITIONAL_cond_fail al_map_rgba(255, 0, 0, 255)
#define CONDITIONAL_cond_succeed al_map_rgba(0, 255, 0, 255)
#define CONDITIONAL_cond_both al_map_rgba(0, 0, 0, 0)

#define WIREFRAME_COLOUR al_map_rgba(255, 255, 255, 100)

#define PREVIEW_RENDER_FPS 10
#define PREVIEW_UPDATE_DELAY_MS 100
#define PREVIEW_DELAY_PER_GRAPH 20
#define PREVIEW_SPIN_PER_FRAME 0.6
#define PREVIEW_EDGES_PER_RENDER 60
#define PREVIEW_BACKGROUND al_col_black
#define PREVIEW_INACTIVE_HIGHLIGHT al_map_rgb(40, 0, 0)
#define PREVIEW_ACTIVE_HIGHLIGHT al_map_rgb(0, 40, 0)

#define MAIN_BACKGROUND_COLOUR al_map_rgba(0, 0, 0, 255)
#define GRAPH_LOW_B 70
#define GRAPH_FAR_A 300

#define DEFAULT_EDGE_CALL al_col_purple
#define DEFAULT_EDGE_OLD al_col_white
#define DEFAULT_EDGE_RET al_col_orange
#define DEFAULT_EDGE_LIB al_col_green
#define DEFAULT_EDGE_NEW al_col_yellow
#define DEFAULT_EDGE_EXCEPT al_col_cyan

#define DEFAULT_NODE_STD al_col_yellow
#define DEFAULT_NODE_JUMP al_col_red
#define DEFAULT_NODE_CALL al_col_purple
#define DEFAULT_NODE_RET al_col_orange
#define DEFAULT_NODE_EXT al_col_green
#define DEFAULT_NODE_EXCEPT al_col_cyan

#define HIGHLIGHT_LINE_COLOUR al_col_green
#define HIGHLIGHT_LINE_PROTRUSION 3000
#define ACTIVITY_LINE_COLOUR al_col_red

#define ANIMATION_FADE_RATE 0.07
#define ANIMATION_UPDATES_PER_FRAME 500
#define MAINGRAPH_DEFAULT_RENDER_FREQUENCY 25

#define DEFAULT_MAX_TRACE_BUFSIZE 400000

//mazimum number of args to store per external
#define DEFAULT_MAX_ARG_STORAGE 100

#define DEFAULT_MAX_WAIT_FRAMES 180