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

#include <new> // for in-place constructor
#include <string>
#include <list>

#include "libwebsockets.h"

struct lws_context_creation_info info;
struct libwebsocket_context * context;
struct libwebsocket * wsi;
libwebsocket_protocols protocols[1];
size_t rx_buffer_size = 4096;
static int nextid = 1;
char ads_port[256 + 30];
int port = 8080;

// message to send: TODO: turn into a list of msgs!
t_symbol * msg;
class ws;
std::list<ws *> maxobjects;

struct Packet {
	unsigned char pre[LWS_SEND_BUFFER_PRE_PADDING];
	unsigned char data[512];
	unsigned char post[LWS_SEND_BUFFER_POST_PADDING];
	size_t len;
};

struct SessionData {
	struct libwebsocket * wsi;
	int id;
	int fd;
	
	std::string received;
	t_dictionary * received_dict;
	t_symbol * received_dict_name;
	
	SessionData() {
		received_dict_name = symbol_unique();
		received_dict = dictionary_new();
	}
	
	~SessionData() {
		object_release((t_object *)received_dict);
	}
};


static t_class * max_class = 0;

class ws {
public:
	t_object ob; // max objwst, must be first!
	
	void * outlet_frame;
	void * outlet_msg;
	
	ws() {
		outlet_msg = outlet_new(&ob, 0);
		outlet_frame = outlet_new(&ob, 0);
		
		maxobjects.push_back(this);
	}
	
	~ws() {
		maxobjects.remove(this);
	}
};

void * ws_new(t_symbol *s, long argc, t_atom *argv) {
	ws *x = NULL;
	if ((x = (ws *)object_alloc(max_class))) {
		
		x = new (x) ws();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		
	}
	return (x);
}

void ws_free(ws *x) {
	x->~ws();
}

void ws_assist(ws *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void ws_log_emit_function(int level, const char *line) {
	post("ws level %d: %s", level, line);
}

int static_callback_ws(struct libwebsocket_context *context,
						struct libwebsocket *wsi,
						enum libwebsocket_callback_reasons reason,
					    void *user,
						void *in, size_t len) {
	// warning: this will be NULL for a new session:
	struct SessionData * session = (struct SessionData *)user;

	char errorstring[1024];
	t_atom a[3];
	Packet packet;
	
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED: {
			session->wsi = wsi;
			session->id = nextid++;
			post("websocket established: session %d", session->id);
			
			// placement new to initialize the C++ stuff:
			void *p = new (session) SessionData;
			post("p %p session %p", p, session);
			
			//				pss->ringbuffer_tail = ringbuffer_head;
		} break;
		case LWS_CALLBACK_PROTOCOL_DESTROY:
			if (session) {
				post("websocket closed: session %d", session->id);
				// C++ teardown:
				session->~SessionData();
			}
			break;
		
		case LWS_CALLBACK_SERVER_WRITEABLE: {
				post("websocket writeable: session %d", session->id);
				if (msg && msg->s_name) {
					
					size_t len = strlen(msg->s_name);
					packet.len = len;
					memccpy(packet.data, msg->s_name, 1, len);
					
					int m = libwebsocket_write(wsi, packet.data, len, LWS_WRITE_TEXT);
					if (m < 0) {
						error("ERROR %d writing to di socket", m);
						return -1;
					}
					if (m < len) {
						error("partial write %d", m);
						return -1;
					}
				}
			} break;
			
		case LWS_CALLBACK_RECEIVE: {
			//printf("websocket receive from session %d: %s", session->id, in);
			const size_t remaining = libwebsockets_remaining_packet_payload(wsi);
			const char * buf = (const char *)in;
			
			if (0 == remaining && libwebsocket_is_final_fragment(wsi)) {
				// either unfragmented message or final part of fragmented message:
				size_t buflen = len;
				if (session->received.length()) {
					//post("final part of fragmented message");
					session->received.append(buf, len);
					buf = session->received.data();
					buflen = session->received.length();
				}
				
				// problem here is Max will truncate the message. The t_symbol is actually OK, but there's hardly any useful way to use it in Max yet.
				// we could have a parsetype attribute, and either a) convert to Dict, or b) to jit.matrix 1 char N
				
				// now output:
//				if (parse_type == ParseTypeJSON) {
//			
//					
//					dictobj_unregister(session->received_dict);
//					char errstring[256];
//					t_max_err err = dictobj_dictionaryfromstring(&session->received_dict, buf, 1, errstring);
//					if (err) {
//						object_error(&ob, "error parsing message as JSON: %s", errstring);
//					} else {
//						
//						dictobj_register(session->received_dict, &session->received_dict_name);
//						
//						atom_setlong(a+0, session->id);
//						atom_setsym(a+1, _sym_dictionary);
//						atom_setsym(a+2, session->received_dict_name);
//						outlet_list(outlet_frame, NULL, 3, a);
//					}
//					
//				} else {
				atom_setlong(a+0, session->id);
				atom_setsym(a+1, gensym(buf));
				for (auto& obj : maxobjects) {
					outlet_list(obj->outlet_frame, NULL, 2, a);
				}
				
				//				}
				
				// done with buffer:
				if (session->received.length()) {
					session->received.clear();
				}
				
			} else {
				//post("partial receive");
				// append to session buffer:
				session->received.append(buf, len);
			}
			
		} break;
			
		default:
			break;
	}
	
	return 0;
}


void ws_bang(ws * x) {
	libwebsocket_service(context, 0);	// zero timeout
}

void ws_send(ws * x, t_symbol * s) {
	msg = s;
	if (context && wsi) {
		post("sending %s", msg->s_name);
		//libwebsocket_callback_on_writable(context, wsi);
		ws_bang(x);
	}
}


void ext_main(void *r)
{
	t_class *c;
	
	lws_set_log_level(LLL_NOTICE, ws_log_emit_function);
	post("websocket: using libwebsockets version %s", lws_get_library_version());

	memset(protocols, 0, 1*sizeof(libwebsocket_protocols));
	// the zeroth sub-protocol is the one that implements HTTP, and websockets whose protocol is undefined:
	protocols[0].name = "default";
	protocols[0].callback = static_callback_ws;
	protocols[0].per_session_data_size = sizeof(SessionData);
	protocols[0].rx_buffer_size = rx_buffer_size;
	
	int opts = 0;
	//if (relax_ssl) opts |= LWS_SERVER_OPTION_ALLOW_NON_SSL_ON_SSL_PORT
	//if (use_ev) opts |= LWS_SERVER_OPTION_LIBEV;

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));
	info.port = port;
	info.iface = NULL;
	info.protocols = &protocols[0];
	info.extensions = libwebsocket_get_internal_extensions();
	info.token_limits = NULL;
	info.ssl_private_key_password = NULL;
	info.ssl_cert_filepath = NULL;
	info.ssl_private_key_filepath = NULL;
	info.ssl_ca_filepath = NULL;
	info.ssl_cipher_list = NULL;
	info.http_proxy_address = NULL;
	info.http_proxy_port = 0;
	info.gid = -1;
	info.uid = -1;
	info.options = opts;
	info.user = NULL;
	info.ka_time = 0;
	info.ka_probes = 0;
	info.ka_interval = 0;
	
	context = libwebsocket_create_context(&info);
	if (context == NULL) {
		error("libwebsocket init failed");
		return;
	}
	
	std::string address("127.0.0.1");
	sprintf(ads_port, "%s:%u\n", address.c_str(), port & 65535);
	wsi = libwebsocket_client_connect(context,
									  "127.0.0.1",
									  port,
									  0,
									  "/",
									  ads_port,
									  "origin",
									  NULL,
									  -1);
	if (!wsi) {
		error("failed to connect");
		return;
	}
	
	c = class_new("ws", (method)ws_new, (method)ws_free, (long)sizeof(ws),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)ws_assist,			"assist",		A_CANT, 0);
	
	class_addmethod(c, (method)ws_bang,	"bang",	0);
	class_addmethod(c, (method)ws_send,	"send",	A_SYM, 0);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}

