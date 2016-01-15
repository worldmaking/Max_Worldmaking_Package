
#include <new> // for in-place constructor
#include <string>
#include <list>
#include <iostream>
#include <set>
#include <map>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl> > con_list;


// a bunch of likely Max includes:
extern "C" {
#include "ext.h"
#include "ext_obex.h"
#include "ext_dictionary.h"
#include "ext_dictobj.h"
#include "ext_systhread.h"
	
#include "z_dsp.h"
	
#include "jit.common.h"
#include "jit.gl.h"
}

// TODO: check any multi-threading issues?

class ws;
class Server;
static std::map <t_atom_long, Server *> server_map;

static t_class * max_class = 0;


class Server {
	
	// this can throw an exception.
	Server(t_atom_long port) : port(port) {
		server.set_open_handler(bind(&Server::on_open,this,websocketpp::lib::placeholders::_1));
		server.set_close_handler(bind(&Server::on_close,this,websocketpp::lib::placeholders::_1));
		server.set_message_handler(bind(&Server::on_message,this,websocketpp::lib::placeholders::_1,websocketpp::lib::placeholders::_2));
		server.init_asio();
		server.listen(port);
		post("created server listening on port %i", port);
		server.start_accept();
		
		// make sure this doesn't get duplicated:
		server_map[port] = this;
	}
	
public:
	server server;
	t_atom_long port;
	con_list clients;				// a set of client client_connections as websocketpp::connection_hdl
	
	std::list<ws *> maxobjects;		// is this still needed?
	
	~Server() {
		// any teardown necessary?
	}
	
	// this can throw an exception:
	static Server * get(t_atom_long port) {
		// does a server already exist for this port?
		auto existing_server = server_map.find(port);
		if (existing_server != server_map.end()) {
			// just use that one:
			return existing_server->second;
		} else {
			// start a new one:
			return new Server(port);
		}
	}
	
	void add(ws * listener) {
		maxobjects.push_back(listener);
	}
	
	void remove(ws * listener) {
		maxobjects.remove(listener);
		
		// TODO: should we check whether to destroy the server at this point, if there are no more listeners?
	}
	
	void bang() {
		int limit = 100; // just a safety net to make sure we don't block Max due to too much IO
		while (limit-- && server.poll_one()) {};
	}
	
	void send(const std::string& msg) {
		for (auto client : clients) {
			server.send(client, msg, websocketpp::frame::opcode::text);
		}
	}
	
	void on_open(websocketpp::connection_hdl hdl) {
		clients.insert(hdl);
	}
	
	void on_close(websocketpp::connection_hdl hdl) {
		clients.erase(hdl);
	}
	
	void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg);
};


class ws {
public:
	t_object ob; // max objwst, must be first!
	
	void * outlet_frame;
	void * outlet_msg;
	
	t_atom_long port;
	
	Server * server;
	
	ws() {
		outlet_msg = outlet_new(&ob, 0);
		outlet_frame = outlet_new(&ob, 0);
		port = 8080;
		server = 0;
	}
	
	void post_attr_init() {
		// will either get an existing or create a new server:
		try {
			server = Server::get(port); // can throw
			server->add(this);
		} catch (const std::exception& ex) {
			object_error(&ob, "failed to create server: %s", ex.what());
		}
	}
	
	~ws() {
		server->remove(this);
	}
	
	void bang() {
		if (server) server->bang();
	}
	
	void send(t_symbol * s) {
		if (server) server->send(std::string(s->s_name));
	}
};

void Server::on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
	for (auto x : maxobjects) {
		outlet_anything(x->outlet_frame, gensym(msg->get_payload().c_str()), 0, 0);
	}
}


void * ws_new(t_symbol *s, long argc, t_atom *argv) {
	ws *x = NULL;
	if ((x = (ws *)object_alloc(max_class))) {
		x = new (x) ws();
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		// invoke any initialization after the attrs are set from here:
		x->post_attr_init();
	}
	return (x);
}

void ws_free(ws *x) {
	x->~ws();
}

void ws_assist(ws *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "bang (to poll), send");
	}
	else {	// outlet
		switch (a) {
			case 0: sprintf(s, "received messages"); break;
			case 1: sprintf(s, "status messages"); break;
		}
	}
}


void ws_bang(ws * x) {
	x->bang();
}

void ws_send(ws * x, t_symbol * s) {
	x->send(s);
}

void ext_main(void *r)
{
	t_class *c;
	
	c = class_new("ws", (method)ws_new, (method)ws_free, (long)sizeof(ws), 0L, A_GIMME, 0);
	class_addmethod(c, (method)ws_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)ws_bang,	"bang",	0);
	class_addmethod(c, (method)ws_send,	"send",	A_SYM, 0);
	
	CLASS_ATTR_LONG(c, "port", 0, ws, port);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

