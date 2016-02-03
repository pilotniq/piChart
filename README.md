# piChart
Software for a nautical chart plotter written in javascript, node.js serverside,
for a Raspberry Pi. See https://hackaday.io/project/9471-pi-chart

Chart/map data is not included. Place your own tile bitmaps in the
server/resources/tiles directory. My implementation assumes offline tiles,
but you should be able to modify it to use online maps, like google maps
for testing.

The server/resources/index.html file assumes tiles using TMS addressing (see
http://www.maptiler.org/google-maps-coordinates-tile-bounds-projection/ for
more on tile addressing). To change this, modify the Openlayers calls in
index.html.

The server/server.js is a node.js program. Start it from the command line with
'node server.js'. It will start a web server on port 8080 serving the chart
plotter application.
