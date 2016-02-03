/*
  piChart server.js

  A web server for a Raspberry Pi based nautical chart plotter.
  
  The plan is for this server to receive GPS data via Bluetooth and send it using
  Websockets to the javascript client.

 */

//Lets require/import the HTTP module
var http = require('http');
var dispatcher = require('httpdispatcher');
var fs = require('fs');
var LatLon = require( 'mt-latlon' );

// socket.io is work in progress.

//Lets define a port we want to listen to
const PORT=8080; 

if ( typeof String.prototype.endsWith != 'function' )
{
    String.prototype.endsWith = function( str ) { return this.substring( this.length - str.length, this.length ) === str; }
};

//Create a server
var server = http.createServer(handleRequest);

var io = require('socket.io')(server);

//Lets start our server
server.listen(PORT, function(){
	//Callback triggered when server is successfully listening. Hurray!
	console.log("Server listening on: http://localhost:%s", PORT);
    });

// Create websocket server

io.on( 'connection', function() { console.log( "io connection" ); } );

dispatcher.setStaticDirname(__dirname);
dispatcher.setStatic( '.' );
// dispatcher.setStatic( 'resources' );

var renderHtml = function(res, content) {
    res.writeHead(200, {'Content-Type': 'text/html'});
    res.end(content, 'utf-8');
}

var renderSVG = function(res, content) {
    res.writeHead(200, {'Content-Type': 'image/svg'});
    res.end(content, 'utf-8');
}

//some private methods
    var serverError = function(response, code, content) {
	response.writeHead(code, {'Content-Type': 'text/plain'});
	response.end(content);
    }

//A sample GET request    
dispatcher.onGet("/", function(req, res) {
	fs.readFile('resources/index.html', function(error, content) {
		if (error) {
		    serverError(500);
		} else {
		    renderHtml(res, content);
		}
	    });
	//	res.writeHead(200, {'Content-Type': 'text/plain'});
	//res.end('Page One');
    });    

dispatcher.onError(function(req, res) {
    res.writeHead(404);
    res.end("Error" );
    });

function stringStartsWith (string, prefix) {
    return string.slice(0, prefix.length) == prefix;
}

//Lets use our dispatcher
function handleRequest(request, response){
    try
    {
        //log the request on console
        console.log(request.url);
        //Disptach
	dispatcher.dispatch(request, response);
    } catch(err) {
        console.log(err);
    }
}

var waypoints = [ { position: new LatLon(59.03685, 17.998 ), speed: 2.0 },
		  { position: new LatLon(59.0357, 18.0042 ), speed: 4.0 },
		  { position: new LatLon(59.0241, 18.0133 ), speed: 4.0 },
  		  { position: new LatLon(59.0154, 18.0289 ), speed: 5.0 },
  		  { position: new LatLon(59.0171, 18.0471 ), speed: 5.0 },
  		  { position: new LatLon(59.0347, 18.06384 ), speed: 5.0 } ];

		  
var boatInfo = { position: waypoints[0].position, COG: 45, HDG: 45, VMG: waypoints[0].speed };

var waypointIndex = 0;
// calculate initial heading between waypoint 0 and 1
boatInfo.COG = 45; // waypoints[0].position.bearingTo( waypoints[1].position );
boatInfo.HDG = boatInfo.COG;

var speed = waypoints[0].speed * 0.5144; // knots to m/s
var lastTime = new Date();

function updateBoat()
{
    var now = new Date();
    var dt = now - lastTime;
    lastTime = now;

    boatInfo.position = boatInfo.position.destinationPoint( boatInfo.COG, dt * speed / (1000 * 1000) );

    var nextWaypoint = waypoints[ waypointIndex + 1 ];
    if( boatInfo.position.distanceTo( nextWaypoint.position ) < 0.005 )
    {
	// less than five meters to the waypoint, switch waypoint
	waypointIndex++;
	speed = waypoints[ waypointIndex ].speed * 0.5144;;
    }

    boatInfo.COG = boatInfo.position.bearingTo( waypoints[ waypointIndex + 1 ].position );
    boatInfo.HDG = boatInfo.COG;

    // should convert speed back to knots here
    io.emit( 'navInfo', { position: [boatInfo.position.lon(), boatInfo.position.lat()], COG: boatInfo.COG, HDG: boatInfo.HDG, speed: speed } );
}

/* Set up boat simulation */
// 5 Hz update rate to start with
setInterval( updateBoat, 500 );

