
#ifndef _AlienorumSphereImpostor
#define _AlienorumSphereImpostor

// Deliberately does not include globals.h (which pulls SDL_opengl.h): sphere_impostor.cpp
// includes imgui's own bundled GL loader for its raw shader/VAO/VBO calls, and that loader
// conflicts (duplicate/incompatible PFNGL...PROC typedefs) with SDL_opengl.h in the same
// translation unit -- see imgui_impl_opengl3_loader.h's own comment about this. Keeping this
// header's dependency down to just imgui.h (for ImVec2/ImU32) means any file including it
// (e.g. visuals.cpp, which does include globals.h/SDL_opengl.h) never pulls the loader in.
#include "imgui.h"

namespace alienorum
{
    // Queues a GPU-rendered sphere impostor (see GPU_SPHERE_RENDERING_PLAN.md) into ImGui's
    // background draw list. For now (implementation phase 3) this just fills the true
    // perspective-projected silhouette with a flat color; texturing and lighting land in later
    // phases.
    //
    // cx,cy,cz,r are the sphere's center and radius in the app's existing "camera space" --
    // i.e. after to_viewer_plane() and the azimuth/altitude rotation Cartesian2D itself applies,
    // but before its perspective divide (see point.cpp) -- in the same distance units as
    // everything else in the app (meters). `zoom` is the same perspective scale factor
    // Cartesian2D uses. dispcx/dispcy convert the resulting screen-space ("zdes") coordinates
    // to pixels, matching every other on-screen primitive in this app.
    //
    // The screen-space bounding box actually used is computed from the true tangent-line
    // geometry (exact at any distance/angle -- see sphere_impostor.cpp), not from projecting
    // the 3D center and assuming a symmetric circle around it, which breaks down badly at
    // close range / large angular size (e.g. a low-orbit satellite looking at a planet).
    // The bounds are written to *out_xmin/xmin/xmax/ymax (screen pixels) for the caller to
    // reuse (selection bounding box, etc). Returns false (and writes nothing) if the camera
    // is degenerate for this object (e.g. inside the sphere along some axis).
    bool queue_sphere_impostor(double cx, double cy, double cz, double r, double zoom,
        double dispcx, double dispcy, ImU32 color,
        double *out_xmin, double *out_ymin, double *out_xmax, double *out_ymax);
}

#endif
