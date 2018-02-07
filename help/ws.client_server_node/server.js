var WebSocketServer = require('websocket').server;
var http = require('http');

var server = http.createServer(function(request, response) {
	console.log("received http");
});
server.listen(8080, function() {});

wsServer = new WebSocketServer({
	httpServer: server
});

wsServer.on('request', function(request) {
	var connection = request.accept(null, request.origin);
	console.log("received connection");
	
	connection.on('message', function(message) {
		if (message.type === 'utf8') {
			console.log("received utf8 message", message.utf8Data);
		} else {
			console.log("received non-utf8 message, don't know what to do");
		}
		
		connection.sendUTF("hi back to you");
	});

	connection.on('close', function(connection) {
		// close user connection
		console.log("closed");
	});
});

console.log("ok");