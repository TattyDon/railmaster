#pragma once

namespace railmaster::client::ui {

// The interface's look, in window points (origin top-left), drawn in
// screen space after the world: panels of warm dark lacquer framed in
// brass with a soft shadow, bars along the screen's edges, tab-shaped tool
// buttons and small label chips. Not inside glBegin/glEnd.

// A framed panel (dialogs, the books, the lists).
void panel(float x0, float y0, float x1, float y1);
// A brass rule across a panel, under a heading.
void rule(float x0, float x1, float y);
// A bar along the top (`top`) or bottom of the screen.
void bar(float x0, float y0, float x1, float y1, bool top);
// A toolbar button: raised brass when chosen, dark otherwise.
void tab(float x0, float y0, float x1, float y1, bool chosen);
// A small rounded label background (town names, the build stamp).
void chip(float x0, float y0, float x1, float y1, float alpha = 0.62f);
// A highlighted row in a list.
void selection(float x0, float y0, float x1, float y1);

} // namespace railmaster::client::ui
