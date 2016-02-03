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
// socket.io is work in progress.
var io = require('socket.io')(http);

//Lets define a port we want to listen to
const PORT=8080; 

if ( typeof String.prototype.endsWith != 'function' )
{
    String.prototype.endsWith = function( str ) { return this.substring( this.length - str.length, this.length ) === str; }
};

//Create a server
var server = http.createServer(handleRequest);

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

