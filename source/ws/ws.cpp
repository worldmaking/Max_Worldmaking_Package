
// see https://stackoverflow.com/questions/32969289/error-in-websocketpp-library-and-boost-in-windows-visual-studio-2015
#define WEBSOCKETPP_NOEXCEPT 1 
#define WEBSOCKETPP_CPP11_CHRONO 1

#include <new> // for in-place constructor
#include <string>
#include <list>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <map>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl> > con_list;

#include "al_max.h"

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
		server.set_reuse_addr(true);
		server.init_asio();
		server.listen(port);
		post("created server listening on port %i", port);
		server.start_accept();
		server.clear_access_channels(websocketpp::log::alevel::all); // this will turn off everything in console output
		
		post("maximum message size %d", server.get_max_message_size());
		
		received_dict_name = symbol_unique();
		received_dict = dictionary_new();
		atom_setsym(&received_dict_name_atom, received_dict_name);
		
		// make sure this doesn't get duplicated:
		server_map[port] = this;
	}
	
public:
	server server;
	t_atom_long port;
	con_list clients;				// a set of client client_connections as websocketpp::connection_hdl
	
	std::list<ws *> maxobjects;		// the set of max objects using this server
	
	t_dictionary * received_dict;
	t_symbol * received_dict_name;
	t_atom received_dict_name_atom;

	
	~Server() {
		post("closing server on port %d", port);
		server.stop_listening();
		
		// remove from map:
		const auto& it= server_map.find(port);
		server_map.erase(it);
		
		for (auto& client : clients) {
			try{
				server.close(client, websocketpp::close::status::normal, "");
			} catch (std::exception& ec) {
				error("close error %s", ec.what());
			}
		}
		
		// now run once to clear them:
		server.run();
		
		object_release((t_object *)received_dict);
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
		
		// check whether to destroy the server at this point if there are no more listeners
		if (maxobjects.empty()) {
			delete this;
		}
	}
	
	void bang() {
		int limit = 100; // just a safety net to make sure we don't block Max due to too much IO
		while (limit-- && server.poll_one()) {};
	}
	
	void forward(websocketpp::connection_hdl hdl, const std::string& msg) {
		websocketpp::lib::error_code ec;
		auto con = server.get_con_from_hdl(hdl, ec);
		//server.get_connection_pt
		for (websocketpp::connection_hdl client : clients) {
			if (server.get_con_from_hdl(client, ec) != con) {
				server.send(client, msg, websocketpp::frame::opcode::text);
			}
		}
	}
	
	void send(const std::string& msg) {
		//post("sending string length %d", msg.size());
		for (auto client : clients) {
			server.send(client, msg, websocketpp::frame::opcode::text);
		}
	}
	
	void send(void * buffer, size_t size) {
		for (auto client : clients) {
			server.send(client, buffer, size, websocketpp::frame::opcode::binary);
		}
	}
	
	void on_open(websocketpp::connection_hdl hdl) {
		clients.insert(hdl);
	}
	
	void on_close(websocketpp::connection_hdl hdl) {
		clients.erase(hdl);
	}
	
	void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
		// if (msg->get_opcode() == websocketpp::frame::opcode::text) { // else binary, use msg->get_payload()
		const char * buf = msg->get_payload().c_str();
		
		// if a broadcast...
		if (buf[0] == '*') {
			const char * trimmed = &buf[1];
			
			// immediately forward to all *other* clients
			forward(hdl, trimmed);
			
			// also share with server:
			to_max_objects(trimmed);
			
		} else {
			to_max_objects(buf);
		}

	}
	
	void to_max_objects(const char * buf);
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
		open();
	}
	
	~ws() {
		close();
	}

	void close() {
		if (server) {
			server->remove(this);
			server = nullptr;
		}
	}

	void open() {
		close();
		try {
			server = Server::get(port); // can throw
			server->add(this);
		} catch (const std::exception& ex) {
			object_error(&ob, "failed to create server: %s", ex.what());
		}
	}
	
	void bang() {
		if (server) server->bang();
	}
	
	void send(const std::string& s) {
		if (server) server->send(s);
	}
	
	void jit_matrix(t_symbol * name) {
		if (!server) return;
		void * in_mat = jit_object_findregistered(name);
		if (!in_mat) {
			jit_error_code(&ob, JIT_ERR_INVALID_INPUT);
			return;
		}
		
		t_jit_matrix_info in_info;
		char * in_bp;
		
		// lock it:
		long in_savelock = (long)jit_object_method(in_mat, _jit_sym_lock, 1);
		
		// ensure data exists:
		jit_object_method(in_mat, _jit_sym_getdata, &in_bp);
		if (!in_bp) {
			jit_error_code(&ob, JIT_ERR_INVALID_INPUT);
			return;
		}
		jit_object_method(in_mat, _jit_sym_getinfo, &in_info);
		
		size_t dimcount = in_info.dimcount;
		size_t bytelength = in_info.dim[dimcount-1] * in_info.dimstride[dimcount-1];
		server->send(in_bp, bytelength);
	
		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);
	}
};

void Server::to_max_objects(const char * buf) {
	//post("on_message string length %d", msg->get_payload().size());
	
	// just output as string:
	t_symbol * sym = gensym(buf);
	for (auto x : maxobjects) {
		outlet_anything(x->outlet_frame, sym, 0, 0);
	}
}


void * ws_new(t_symbol *s, long argc, t_atom *argv) {
	ws *x = NULL;
	if ((x = (ws *)object_alloc(max_class))) {
		try {
			x = new (x) ws();
			// apply attrs:
			attr_args_process(x, (short)argc, argv);
			// invoke any initialization after the attrs are set from here:
			x->post_attr_init();
		} catch (const std::exception& e) {			
			post(e.what());
			return NULL;
		}
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

void ws_open(ws * x) {
	x->open();
}

void ws_close(ws * x) {
	x->close();
}

void ws_send(ws * x, t_symbol * s) {
	x->send(std::string(s->s_name));
}

// kind of useless...
//extern "C" t_max_err compression_compressjson_headless(char *json, long srclen, t_handle compressedjson);

void ws_dictionary(ws * x, t_symbol * s) {
	t_dictionary *d = dictobj_findregistered_retain(s);
	if (d) {
		t_object *jsonwriter = (t_object *)object_new(_sym_nobox, _sym_jsonwriter);
		t_handle json;
		object_method(jsonwriter, _sym_writedictionary, d);
		object_method(jsonwriter, _sym_getoutput, &json);
		
		/*
		// this compresses the JSON into a hash like maxpats
		// only useful if we can uncompress at the browser end...
		{
			t_handle	compressed_json = sysmem_newhandle(0);
			t_max_err	err;
			err = compression_compressjson_headless(*json, strlen(*json), compressed_json);
			if (!err) {
				x->send(std::string(*compressed_json));
				sysmem_freehandle(compressed_json);
			}
		
		}
		*/
		
		x->send(std::string(*json));
		
		object_free(jsonwriter);
		sysmem_freehandle(json);
	} else {
		object_error(&x->ob, "unable to reference dictionary named %s", s->s_name);
		return;
	}
	dictobj_release(d);
}

void ws_anything(ws * x, t_symbol * s, int argc, t_atom * argv) {
	static const char * separator = " "; // TODO we could have an attribute to set this separator character.
	
	std::stringstream ss;
	
	if (s && s->s_name) {
		ss << s->s_name;
		if (argc) ss << separator;
	}
	
	for (int i = 0; i < argc; i++,argv++) {
		switch (argv->a_type) {
			case A_LONG:
				ss << atom_getlong(argv);
				break;
			case A_FLOAT:
				ss << atom_getfloat(argv);
				break;
			case A_SYM:
				ss << atom_getsym(argv)->s_name;
				break;
			default:
				continue;
		}
		if (i < argc-1) ss << separator;
	}
	
	x->send(ss.str());
}

void ws_jit_matrix(ws *x, t_symbol *s) {
	x->jit_matrix(s);
}


void ws_list(ws * x, t_symbol * s, int argc, t_atom * argv) {
	ws_anything(x, NULL, argc, argv);
}

void ws_test_size(ws * x, t_atom_long size) {
	
	std::string s(size, 'a');
	s[size-1] = '!';
	
	object_post(&x->ob, "size %d %s", size, s.data());
	
	x->send(s);
}

t_max_err ws_port_set(ws *x, t_object *attr, long argc, t_atom *argv) {
	x->port = atom_getlong(argv);
	x->open();
	return 0;
}

void ext_main(void *r)
{
	t_class *c;
	
	common_symbols_init();
	
	c = class_new("ws", (method)ws_new, (method)ws_free, (long)sizeof(ws), 0L, A_GIMME, 0);
	class_addmethod(c, (method)ws_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)ws_bang,	"bang",	0);
	class_addmethod(c, (method)ws_open,	"open",	0);
	class_addmethod(c, (method)ws_close,	"close",	0);
	class_addmethod(c, (method)ws_send,	"send",	A_SYM, 0);
	class_addmethod(c, (method)ws_jit_matrix, "jit_matrix", A_SYM, 0);
	class_addmethod(c, (method)ws_dictionary, "dictionary", A_SYM, 0);
	class_addmethod(c, (method)ws_anything, "anything", A_GIMME, 0);
	class_addmethod(c, (method)ws_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)ws_test_size, "test_size", A_LONG, 0);
	
	CLASS_ATTR_LONG(c, "port", 0, ws, port);
	CLASS_ATTR_ACCESSORS(c, "port", NULL, ws_port_set);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

