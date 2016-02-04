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
var waypointDirection = 1; // -1 when returning
		  
var boatInfo = { position: waypoints[0].position, COG: 45, HDG: 45, VMG: waypoints[0].speed };

var waypointIndex = 0;
// calculate initial heading between waypoint 0 and 1
boatInfo.COG = 45; // waypoints[0].position.bearingTo( waypoints[1].position );
boatInfo.HDG = boatInfo.COG;

var speedUp = 10;
var speed = waypoints[0].speed * 0.5144 * speedUp ; // knots to m/s sped up 10 times for testing.
var lastTime = new Date();

function updateBoat()
{
    var now = new Date();
    var dt = now - lastTime;
    var maxTurnRate = 10;
    lastTime = now;

    boatInfo.position = boatInfo.position.destinationPoint( boatInfo.COG, dt * speed / (1000 * 1000) );

    var nextWaypoint = waypoints[ waypointIndex + waypointDirection ];
    var distanceToNext = boatInfo.position.distanceTo( nextWaypoint.position );
    
    if( distanceToNext < 0.005 * speedUp )
    {
	// less than five meters to the waypoint, switch waypoint
	waypointIndex+=waypointDirection;
	if( (waypointIndex == (waypoints.length - 1)) && (waypointDirection == 1))
	    waypointDirection = -1;
	else if( (waypointIndex == 0) && (waypointDirection == -1))
	    waypointDirection = 1;

	speed = waypoints[ waypointIndex ].speed * 0.5144 * speedUp;
    }

    var bearing = boatInfo.position.bearingTo( waypoints[ waypointIndex + waypointDirection ].position );
    var preCOG = boatInfo.COG;
    var courseDiff = bearing - boatInfo.COG;
    if( courseDiff > maxTurnRate )
	courseDiff = maxTurnRate;
    else if( courseDiff < -maxTurnRate)
	courseDiff = -maxTurnRate;

    boatInfo.COG += courseDiff;
    boatInfo.HDG = boatInfo.COG;

    // should convert speed back to knots here
    io.emit( 'navInfo', { position: [boatInfo.position.lon(), boatInfo.position.lat()], COG: boatInfo.COG, HDG: boatInfo.HDG, speed: speed } );
}

/* Set up boat simulation */
// 5 Hz update rate to start with
setInterval( updateBoat, 500 );

