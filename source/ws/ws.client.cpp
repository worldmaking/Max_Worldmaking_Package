
#include <new> // for in-place constructor
#include <string>
#include <list>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <map>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

typedef websocketpp::client<websocketpp::config::asio_client> client;
typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl> > con_list;

// a bunch of likely Max includes:
#include "al_max.h"

// TODO: check any multi-threading issues?

static t_class * max_class = 0;

class ws_client {
public:
	t_object ob;
	
	void * outlet_frame;
	void * outlet_msg;
	
	t_atom_long port = 8080;
	t_atom_long poll_limit = 1000;
	t_atom_long autoconnect = 1;
	t_symbol * host;
	
	t_atom_long connected = 0;
	
	client wsclient;
	client::connection_ptr con = 0;
	websocketpp::connection_hdl connection_hdl;
	
	ws_client() {
		host = gensym("localhost");

		outlet_msg = outlet_new(&ob, 0);
		outlet_frame = outlet_new(&ob, 0);
	}
	
	void post_attr_init() {
		try {
			// set logging policy if needed
			wsclient.clear_access_channels(websocketpp::log::alevel::frame_header);
			wsclient.clear_access_channels(websocketpp::log::alevel::frame_payload);
			//wsclient.set_error_channels(websocketpp::log::elevel::none);
			
			// Initialize ASIO
			wsclient.set_reuse_addr(true);
			wsclient.init_asio();
			wsclient.start_perpetual(); // allows reconnection
		
			// Register our handlers
			wsclient.set_open_handler(bind(&ws_client::on_open,this,websocketpp::lib::placeholders::_1));
			wsclient.set_fail_handler(bind(&ws_client::on_fail,this,websocketpp::lib::placeholders::_1));
			wsclient.set_message_handler(bind(&ws_client::on_message,this,websocketpp::lib::placeholders::_1,websocketpp::lib::placeholders::_2));
			wsclient.set_close_handler(bind(&ws_client::on_close,this,websocketpp::lib::placeholders::_1));
			
		} catch (const std::exception& ex) {
			object_error(&ob, "failed to create client: %s", ex.what());
		} catch (websocketpp::lib::error_code e) {
			object_error(&ob, "failed to create websocket: %s", e.message().c_str());
		} catch (...) {
			object_error(&ob, "other exception connecting client");
		}
		
		if (autoconnect) open();
	}
	
	~ws_client() {
		if (connected) close();
		wsclient.stop_perpetual();
	}
	
	void open() {
		
		std::string uri = "ws://" + std::string(host->s_name) + ":" + std::to_string(port);
		object_post(&ob, "connecting to %s", uri.c_str());

		if (connected) {
			object_warn(&ob, "connection already open");
			return;
		}
		
		try {
			websocketpp::lib::error_code ec;
			con = wsclient.get_connection(uri, ec);
			if (!con) {
				object_error(&ob, "failed to create connection to %s: %s", uri.c_str(), ec.message().c_str());
			} else {
				wsclient.connect(con);
				object_post(&ob, "ws.client connecting to %s...", uri.c_str());
			}
			
		} catch (const std::exception& ex) {
			object_error(&ob, "failed to create client: %s", ex.what());
		} catch (websocketpp::lib::error_code e) {
			object_error(&ob, "failed to create websocket: %s", e.message().c_str());
		} catch (...) {
			object_error(&ob, "other exception connecting client");
		}
	}
	
	void close(const std::string& reason = "") {
		if (!connected) {
			object_warn(&ob, "no open connection to close");
			return;
		}
		wsclient.close(connection_hdl, websocketpp::close::status::normal, reason);
	}
	
	void bang() {
		int limit = poll_limit; // just a safety net to make sure we don't block Max due to too much IO
		while (limit-- && wsclient.poll_one()) {};
	}
	
	void send(const std::string& msg) {
		if (!connected) return;
		
		wsclient.send(connection_hdl,msg,websocketpp::frame::opcode::text);
		//object_post(&ob, "Sent Message: %s", msg.c_str());
	}
	
	void jit_matrix(t_symbol * name) {
		if (!connected) return;
		
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
		wsclient.send(connection_hdl, in_bp, bytelength, websocketpp::frame::opcode::binary);
		
		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);
	}
	
	////// websocketpp handlers //////
	
	void on_open(websocketpp::connection_hdl hdl) {
		connected = 1;
		connection_hdl = hdl;
		
		t_atom a[1];
		atom_setlong(a, connected);
		outlet_anything(outlet_msg, gensym("connected"), 1, a);
	}
	
	void on_close(websocketpp::connection_hdl hdl) {
		connected = 0;
		//connection_hdl = 0;
		
		con = 0;
		
		t_atom a[1];
		atom_setlong(a, connected);
		outlet_anything(outlet_msg, gensym("connected"), 1, a);
		
		// this needs to be delayed by a period of time
		//if (autoconnect) open();
	}

	void on_fail(websocketpp::connection_hdl hdl) {
		connected = 0;
		//connection_hdl = 0;
		
		object_error(&ob, "local reason %s, remote reason %s, %s", con->get_local_close_reason().c_str(), con->get_remote_close_reason().c_str(),con->get_ec().message().c_str());
		
		con = 0;
		
		t_atom a[1];
		atom_setlong(a, connected);
		outlet_anything(outlet_msg, gensym("connected"), 1, a);
		
		// this needs to be delayed by a period of time
		//if (autoconnect) open();
	}

	void on_message(websocketpp::connection_hdl hdl, client::message_ptr msg) {
		outlet_anything(outlet_frame, gensym(msg->get_payload().c_str()), 0, 0);
	}
};



void * ws_new(t_symbol *s, long argc, t_atom *argv) {
	ws_client *x = NULL;
	if ((x = (ws_client *)object_alloc(max_class))) {
		try {
			x = new (x) ws_client();
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

void ws_free(ws_client *x) {
	x->~ws_client();
}

void ws_assist(ws_client *x, void *b, long m, long a, char *s)
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

void ws_open(ws_client * x) {
	x->open();
}

void ws_close(ws_client * x) {
	x->close();
}

void ws_bang(ws_client * x) {
	x->bang();
}

void ws_send(ws_client * x, t_symbol * s) {
	x->send(std::string(s->s_name));
}

// kind of useless...
//extern "C" t_max_err compression_compressjson_headless(char *json, long srclen, t_handle compressedjson);

void ws_dictionary(ws_client * x, t_symbol * s) {
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

void ws_anything(ws_client * x, t_symbol * s, int argc, t_atom * argv) {
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


void ws_list(ws_client * x, t_symbol * s, int argc, t_atom * argv) {
	ws_anything(x, NULL, argc, argv);
}

void ws_jit_matrix(ws_client *x, t_symbol *s) {
	x->jit_matrix(s);
}

void ws_test_size(ws_client * x, t_atom_long size) {
	
	std::string s(size, 'a');
	s[size-1] = '!';
	
	object_post(&x->ob, "size %d %s", size, s.data());
	
	x->send(s);
}

t_max_err ws_port_set(ws_client *x, t_object *attr, long argc, t_atom *argv) {
	x->port = atom_getlong(argv);
	x->open();
	return 0;
}

t_max_err ws_host_set(ws_client *x, t_object *attr, long argc, t_atom *argv) {
	x->host = atom_getsym(argv);
	x->open();
	return 0;
}

void ext_main(void *r)
{
	t_class *c;
	
	common_symbols_init();
	
	c = class_new("ws.client", (method)ws_new, (method)ws_free, (long)sizeof(ws_client), 0L, A_GIMME, 0);
	class_addmethod(c, (method)ws_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)ws_bang,	"bang",	0);
	class_addmethod(c, (method)ws_open,	"open",	0);
	class_addmethod(c, (method)ws_close, "close", 0);
	class_addmethod(c, (method)ws_send,	"send",	A_SYM, 0);
	class_addmethod(c, (method)ws_jit_matrix, "jit_matrix", A_SYM, 0);
	class_addmethod(c, (method)ws_dictionary, "dictionary", A_SYM, 0);
	class_addmethod(c, (method)ws_anything, "anything", A_GIMME, 0);
	class_addmethod(c, (method)ws_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)ws_test_size, "test_size", A_LONG, 0);
	
	CLASS_ATTR_LONG(c, "port", 0, ws_client, port);
	CLASS_ATTR_ACCESSORS(c, "port", NULL, ws_port_set);
	CLASS_ATTR_SYM(c, "host", 0, ws_client, host);
	CLASS_ATTR_ACCESSORS(c, "host", NULL, ws_host_set);
	CLASS_ATTR_LONG(c, "poll_limit", 0, ws_client, poll_limit); // = max events per bang
	CLASS_ATTR_LONG(c, "autoconnect", 0, ws_client, autoconnect); // = max events per bang
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

