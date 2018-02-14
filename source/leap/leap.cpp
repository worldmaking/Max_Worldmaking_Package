/**
	@file
	leap - a max object
 
	Inspired by and partially derived from https://github.com/akamatsu/aka.leapmotion/ (http://akamatsu.org/aka/max/objects/) (Masayuki Akamatsu), which is shared under Creative Commons Attribution 3.0 Unported License.
 
	Updated to V2 SDK and extended by Graham Wakefield, 2015
 
 */

#include "Leap.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "ext.h"
#include "ext_obex.h"
#include "ext_dictobj.h"
#include "jit.common.h"
#include "jit.gl.h"
#ifdef __cplusplus
}
#endif

#include <new>

#define BONEPOINTS_PER_FINGER 5

t_class *leap_class;
static t_symbol * ps_frame_start;
static t_symbol * ps_frame_end;
static t_symbol * ps_frame;
static t_symbol * ps_hand;
static t_symbol * ps_finger;
static t_symbol * ps_palm;
static t_symbol * ps_ball;
static t_symbol * ps_connected;
static t_symbol * ps_fps;
static t_symbol * ps_probability;

class t_leap {
public:
	
	struct Quaternion	{
	public:
		
		t_atom atoms[4];	// x, y, z, w
		
		Quaternion() {
			atom_setfloat(atoms+0, 0.);
			atom_setfloat(atoms+1, 0.);
			atom_setfloat(atoms+2, 0.);
			atom_setfloat(atoms+3, 1.);
		}
		
		Quaternion(const Leap::Matrix& basis, bool isRight) {
			fromBasis(basis, isRight);
		}
		
		void fromBasis(const Leap::Matrix& basis, bool isRight) {
			const Leap::Vector& xBasis = basis.xBasis;
			const Leap::Vector& yBasis = basis.yBasis;
			const Leap::Vector& zBasis = basis.zBasis;
			double x, y, z;
			
			double w = sqrtf(1.0 + xBasis.x + yBasis.y + zBasis.z) / 2.0;
			double rw4 = -1./(4.0 * w);
			
			// possible that the bases could be flipped
			// this is easily done by switching the sign of x,y,z
			
			if (isRight) {
				x = (zBasis.y - yBasis.z) * rw4;
				y = (xBasis.z - zBasis.x) * rw4;
				z = (yBasis.x - xBasis.y) * rw4;
			} else {
				x = (zBasis.y - yBasis.z) * rw4;
				y = (-xBasis.z - zBasis.x) * rw4;
				z = (yBasis.x - -xBasis.y) * rw4;
			}
			// normalize here:
			double m = 1./sqrtf(x*x+y*y+z*z+w*w);
			atom_setfloat(atoms+0, x*m);
			atom_setfloat(atoms+1, y*m);
			atom_setfloat(atoms+2, z*m);
			atom_setfloat(atoms+3, w*m);
		}
		
	};
	
	struct HandMatCell {
		float x, y, z;
		float qx, qy, qz, qw;
		float sx, sy, sz;
//		float x, y, z;
//		float tx, ty;
//		float nx, ny, nz;
	};
	
	// structure of the binary data for frame serialization/deserialization
	// (needs a header with data length)
	struct SerializedFrame {
	public:
		int32_t length;
		unsigned char data[16380];	// i.e. total frame size is 16384
	};

	class LeapListener : public Leap::Listener {
	public:
		t_leap * owner;
		
		// do nothing -- we're going to poll with bang() instead:
//		virtual void onFrame(const Leap::Controller&) {}
//		virtual void onDeviceChange(const Leap::Controller &) {}
//		virtual void onFocusGained(const Leap::Controller &) {}
//		virtual void onFocusLost(const Leap::Controller &) {}
//		virtual void onServiceConnect(const Leap::Controller &) {}
//		virtual void onServiceDisconnect(const Leap::Controller &) {}
//		virtual void onDisconnect(const Leap::Controller &) {}
	
		// use this callback to set the policy flags
		virtual void onConnect(const Leap::Controller&) { owner->configure(); }
	};

    t_object	ob;			// the object itself (must be first)
    
	int			unique;		// only output new data
	int			allframes;	// output all frames between each poll (rather than just the latest frame)
	int			serialize;	// output serialized frames
	int 		images;		// output the raw images
	int			motion_tracking;
	int			hmd;		// optimize for LeapVR HMD mount
	int			background;	// capture data even when Max has lost focus
	int			aka;		// output in a form compatible with aka.leapmotion
	
	int			gesture_any;	// accept any gesture
	int			gesture_swipe, gesture_circle, gesture_screen_tap, gesture_key_tap;	// enable specific gestures
	
	t_symbol *	config;
	t_dictionary * config_dict;
	t_symbol *	gesture_dict_name;
	t_dictionary * gesture_dict;
	
	void *		outlet_frame;
	void *		outlet_image[2];
	void *		outlet_hands;
	void *		outlet_gesture;
	void *		outlet_tracking;
	void *		outlet_distortion;
	void *		outlet_msg;
	
	// matrices for the IR images:
	void *		image_wrappers[2];
	void *		image_mats[2];
	void *		distortion_image_wrappers[2];
	void *		distortion_image_mats[2];
	int			distortion_dim[2];
	int			image_width, image_height;
	int			distortion_requested;
	void *		hand_mat;
	void *		hand_mat_wrapper;
	HandMatCell *	hand_mat_ptr;
	
	Leap::Controller controller;
	LeapListener listener;
	Leap::Frame lastFrame;
	int64_t		lastFrameID;
	
	// the last-received frame:
	Leap::Frame frame;
	
	t_leap(int index = 0) {
		
		outlet_msg = outlet_new(&ob, 0);
		outlet_distortion = outlet_new(&ob, 0);
		outlet_tracking = outlet_new(&ob, 0);
		outlet_gesture = outlet_new(&ob, 0);
		outlet_hands = outlet_new(&ob, "dictionary");
		outlet_image[0] = outlet_new(&ob, "jit_matrix");
		outlet_image[1] = outlet_new(&ob, "jit_matrix");
        outlet_frame = outlet_new(&ob, 0);

		// attrs:
		unique = 0;
		allframes = 0;
		images = 1;
		aka = 0;
		serialize = 0;
		motion_tracking = 0;
		hmd = 0;
		background = 1;
		
		gesture_any = 0;
		gesture_swipe = 0;
		gesture_circle = 0;
		gesture_screen_tap = 0;
		gesture_key_tap = 0;
		
		config = jit_symbol_unique();
		config_dict = dictionary_new();
		gesture_dict_name = jit_symbol_unique();
		gesture_dict = dictobj_register(dictionary_new(), &gesture_dict_name);
		
		// create jit.matrix for the output images:
		image_width = 0;
		image_height = 0;
		distortion_dim[0] = 0;
		distortion_dim[1] = 0;
		for (int i=0; i<2; i++) {
			// create matrix:
			image_wrappers[i] = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			image_mats[i] = NULL;
			
			distortion_image_wrappers[i] = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			distortion_image_mats[i] = NULL;
		}
		distortion_requested = 1;
		
		hand_mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
		hand_mat = configureMatrix2D(hand_mat_wrapper, (sizeof(HandMatCell)/sizeof(float)), _jit_sym_float32, BONEPOINTS_PER_FINGER, 5);
		jit_object_method(hand_mat, _jit_sym_getdata, &hand_mat_ptr);
		
		// internal:
		lastFrameID = 0;
		listener.owner = this;
        controller.addListener(listener);
    }
    
    ~t_leap() {
		for (int i=0; i<2; i++) {
			object_release((t_object *)image_wrappers[i]);
		}
		object_release((t_object *)config_dict);
		object_release((t_object *)gesture_dict);
    }
	
	void * configureMatrix2D(void * mat_wrapper, long planecount, t_symbol * type, long w, long h) {
		void * mat = jit_object_method(mat_wrapper, _jit_sym_getmatrix);
		t_jit_matrix_info info;
		jit_matrix_info_default(&info);
		info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
		info.planecount = planecount;
		info.type = type;
		info.dimcount = 2;
		info.dim[0] = w;
		info.dim[1] = h;
		jit_object_method(mat, _jit_sym_setinfo_ex, &info);
		return mat;
	}
	
    void configure() {
        int flag = Leap::Controller::POLICY_DEFAULT;
		if (images)		flag |= Leap::Controller::POLICY_IMAGES;
		if (hmd)		flag |= Leap::Controller::POLICY_OPTIMIZE_HMD;
		if (background) flag |= Leap::Controller::POLICY_BACKGROUND_FRAMES;
		controller.setPolicyFlags((Leap::Controller::PolicyFlag)flag);
		
		controller.enableGesture(Leap::Gesture::TYPE_SWIPE, (gesture_any || gesture_swipe));
		controller.enableGesture(Leap::Gesture::TYPE_CIRCLE, (gesture_any || gesture_circle));
		controller.enableGesture(Leap::Gesture::TYPE_SCREEN_TAP, (gesture_any || gesture_screen_tap));
		controller.enableGesture(Leap::Gesture::TYPE_KEY_TAP, (gesture_any || gesture_key_tap));
		
		// https://developer.leapmotion.com/documentation/cpp/api/Leap.Config.html#cppclass_leap_1_1_config
		if (dictionary_hasentry(config_dict, gensym("Gesture"))) {
			t_dictionary * gesture_dict = 0;
			dictionary_getdictionary(config_dict, gensym("Gesture"), (t_object **)&gesture_dict);
			double f;
			
			if (dictionary_hasentry(config_dict, gensym("Circle"))) {
				t_dictionary * sub_dict = 0;
				dictionary_getdictionary(config_dict, gensym("Circle"), (t_object **)&sub_dict);
				if (dictionary_getfloat(sub_dict, gensym("MinRadius"), &f) == 0) {
					controller.config().setFloat("Gesture.Circle.MinRadius", f);
				}
				if (dictionary_getfloat(sub_dict, gensym("MinArc"), &f) == 0) {
					controller.config().setFloat("Gesture.Circle.MinArc", f);
				}
			}
			
			if (dictionary_hasentry(config_dict, gensym("Swipe"))) {
				t_dictionary * sub_dict = 0;
				dictionary_getdictionary(config_dict, gensym("Swipe"), (t_object **)&sub_dict);
				if (dictionary_getfloat(sub_dict, gensym("MinLength"), &f) == 0) {
					controller.config().setFloat("Gesture.Swipe.MinLength", f);
				}
				if (dictionary_getfloat(sub_dict, gensym("MinVelocity"), &f) == 0) {
					controller.config().setFloat("Gesture.Swipe.MinVelocity", f);
				}
			}
			
			if (dictionary_hasentry(config_dict, gensym("ScreenTap"))) {
				t_dictionary * sub_dict = 0;
				dictionary_getdictionary(config_dict, gensym("ScreenTap"), (t_object **)&sub_dict);
				if (dictionary_getfloat(sub_dict, gensym("HistorySeconds"), &f) == 0) {
					controller.config().setFloat("Gesture.ScreenTap.HistorySeconds", f);
				}
				if (dictionary_getfloat(sub_dict, gensym("MinDistance"), &f) == 0) {
					controller.config().setFloat("Gesture.ScreenTap.MinDistance", f);
				}
				if (dictionary_getfloat(sub_dict, gensym("MinForwardVelocity"), &f) == 0) {
					controller.config().setFloat("Gesture.ScreenTap.MinForwardVelocity", f);
				}
			}
			
			if (dictionary_hasentry(config_dict, gensym("KeyTap"))) {
				t_dictionary * sub_dict = 0;
				dictionary_getdictionary(config_dict, gensym("KeyTap"), (t_object **)&sub_dict);
				if (dictionary_getfloat(sub_dict, gensym("HistorySeconds"), &f) == 0) {
					controller.config().setFloat("Gesture.KeyTap.HistorySeconds", f);
				}
				if (dictionary_getfloat(sub_dict, gensym("MinDistance"), &f) == 0) {
					controller.config().setFloat("Gesture.KeyTap.MinDistance", f);
				}
				if (dictionary_getfloat(sub_dict, gensym("MinDownVelocity"), &f) == 0) {
					controller.config().setFloat("Gesture.KeyTap.MinDownVelocity", f);
				}
			}
			controller.config().save();
		}
    }
	
	void serializeAndOutput(const Leap::Frame& frame) {
		t_atom a[1];
		std::string s = frame.serialize();
		size_t len = s.length();
		SerializedFrame * mat_ptr = 0;
		if (len <= sizeof(SerializedFrame)-4) {
			
			// dump via a jit_matrix 1 char len
			
			// export this distortion mesh to Jitter
			t_jit_matrix_info info;
			
			// create matrix:
			void * mat_wrapper = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
			void * mat = jit_object_method(mat_wrapper, _jit_sym_getmatrix);
			
			// configure matrix:
			jit_matrix_info_default(&info);
			info.flags |= JIT_MATRIX_DATA_PACK_TIGHT;
			info.planecount = 1;
			info.type = gensym("char");
			info.dimcount = 1;
			info.dim[0] = 16384;
			jit_object_method(mat, _jit_sym_setinfo_ex, &info);
			
			// copy data:
			jit_object_method(mat, _jit_sym_getdata, &mat_ptr);
			if (mat_ptr) {
				mat_ptr->length = len;
				memcpy(mat_ptr->data, s.data(), len);
				
				// output matrix:
				atom_setsym(a, jit_attr_getsym(mat_wrapper, _jit_sym_name));
				outlet_anything(outlet_msg, gensym("serialized_frame"), 1, a);
			}
			
			// done with matrix:
			object_release((t_object *)mat_wrapper);
		}
	}
	
	t_dictionary * processBone(const Leap::Bone& bone, bool isRight, int idx, int b, t_symbol * name) {
		Quaternion q;
		Leap::Vector vec;
		t_atom avec[4];
		t_dictionary * bone_dict = dictionary_new();

		
		dictionary_appendsym(bone_dict, _sym_name, name);
		dictionary_appendlong(bone_dict, gensym("valid"), bone.isValid());
		dictionary_appendlong(bone_dict, _sym_type, (int)bone.type());
		
		dictionary_appendfloat(bone_dict, gensym("length"), bone.length() * 0.001);
		dictionary_appendfloat(bone_dict, gensym("width"), bone.width() * 0.001);

		q.fromBasis(bone.basis(), isRight);	// or basis.rigidInverse?
		dictionary_appendatoms(bone_dict, gensym("quat"), 4, q.atoms);

		vec = bone.center();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(bone_dict, gensym("center"), 3, avec);
		
		vec = bone.prevJoint();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(bone_dict, gensym("prevJoint"), 3, avec);
		
		HandMatCell& cell0 = hand_mat_ptr[idx*BONEPOINTS_PER_FINGER + b];
		cell0.x = vec.x * 0.001;
		cell0.y = vec.y * 0.001;
		cell0.z = vec.z * 0.001;
		
		vec = bone.nextJoint();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(bone_dict, gensym("nextJoint"), 3, avec);
		if (b==3) {
			HandMatCell& cell1 = hand_mat_ptr[idx*BONEPOINTS_PER_FINGER + (BONEPOINTS_PER_FINGER-1)];
			cell1.x = vec.x * 0.001;
			cell1.y = vec.y * 0.001;
			cell1.z = vec.z * 0.001;
		}
		
		vec = bone.direction();
		atom_setfloat(avec+0, vec.x);
		atom_setfloat(avec+1, vec.y);
		atom_setfloat(avec+2, vec.z);
		dictionary_appendatoms(bone_dict, gensym("direction"), 3, avec);
		
		// update hand matrix:
		
		return bone_dict;
	}
	
	t_dictionary * processFinger(const Leap::Finger& finger, bool isRight, int idx, t_symbol * name) {
		Quaternion q;
		Leap::Vector vec;
		t_atom avec[4];
		t_dictionary * finger_dict = dictionary_new();
		bool isValid = finger.isValid();
		
		dictionary_appendlong(finger_dict, gensym("valid"), isValid);
		const int32_t id = finger.id();
		dictionary_appendlong(finger_dict, _sym_id, id);
		dictionary_appendsym(finger_dict, _sym_type, name);
		//dictionary_appendlong(finger_dict, gensym("frame"), frame_id);
		//dictionary_appendlong(finger_dict, gensym("hand"), hand_id);
		dictionary_appendfloat(finger_dict, gensym("timeVisible"), finger.timeVisible());
		dictionary_appendlong(finger_dict, gensym("extended"), finger.isExtended());
		dictionary_appendfloat(finger_dict, gensym("length"), finger.length() * 0.001);
		dictionary_appendfloat(finger_dict, gensym("width"), finger.width() * 0.001);

		dictionary_appendfloat(finger_dict, gensym("touchDistance"), finger.touchDistance());
		switch (finger.touchZone()) {
			case Leap::Pointable::ZONE_NONE:
				dictionary_appendsym(finger_dict, gensym("touchZone"), gensym("none"));
				break;
			case Leap::Pointable::ZONE_HOVERING:
				dictionary_appendsym(finger_dict, gensym("touchZone"), gensym("hovering"));
				break;
			case Leap::Pointable::ZONE_TOUCHING:
				dictionary_appendsym(finger_dict, gensym("touchZone"), gensym("touching"));
				break;
			default:
				break;
		}

		vec = finger.direction();
		atom_setfloat(avec+0, vec.x);
		atom_setfloat(avec+1, vec.y);
		atom_setfloat(avec+2, vec.z);
		dictionary_appendatoms(finger_dict, gensym("direction"), 3, avec);

		vec = finger.tipPosition();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(finger_dict, gensym("tipPosition"), 3, avec);

		vec = finger.stabilizedTipPosition();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(finger_dict, gensym("stabilizedTipPosition"), 3, avec);

		vec = finger.tipVelocity();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(finger_dict, gensym("tipVelocity"), 3, avec);
		
		// bones:
		t_atom bone_atoms[4];
		for (int b=0; b<4; b++) {
			const Leap::Bone::Type boneType = static_cast<Leap::Bone::Type>(b);
			const Leap::Bone bone = finger.bone(boneType);
			t_symbol * name = 0;
			switch (b) {
				case 0: name = gensym("metacarpal"); break;
				case 1: name = gensym("proximal"); break;
				case 2: name = gensym("intermediate"); break;
				case 3: name = gensym("distal"); break;
				default: break;
			}
			
			t_dictionary * bone_dict = processBone(bone, isRight, idx, b, name);
			dictionary_appendsym(bone_dict, _sym_name, name);
			//dictionary_appenddictionary(bones_dict, name, (t_object *)bone_dict);
			atom_setobj(bone_atoms+b, bone_dict);
		}
		dictionary_appendatoms(finger_dict, gensym("bones"), 4, bone_atoms);
		
		return finger_dict;
	}
	
	t_dictionary * processTool(const Leap::Tool& tool) {
		Quaternion q;
		Leap::Vector vec;
		t_atom avec[4];
		t_dictionary * tool_dict = dictionary_new();
		bool isValid = tool.isValid();
		
		dictionary_appendlong(tool_dict, gensym("valid"), isValid);
		const int32_t id = tool.id();
		dictionary_appendlong(tool_dict, _sym_id, id);
		dictionary_appendlong(tool_dict, gensym("frame"), tool.frame().id());
		dictionary_appendlong(tool_dict, gensym("hand"), tool.hand().id());
		dictionary_appendfloat(tool_dict, gensym("timeVisible"), tool.timeVisible());
		dictionary_appendfloat(tool_dict, gensym("length"), tool.length() * 0.001);
		dictionary_appendfloat(tool_dict, gensym("width"), tool.width() * 0.001);
		dictionary_appendfloat(tool_dict, gensym("touchDistance"), tool.touchDistance());
		switch (tool.touchZone()) {
			case Leap::Pointable::ZONE_NONE:
				dictionary_appendsym(tool_dict, gensym("touchZone"), gensym("none"));
				break;
			case Leap::Pointable::ZONE_HOVERING:
				dictionary_appendsym(tool_dict, gensym("touchZone"), gensym("hovering"));
				break;
			case Leap::Pointable::ZONE_TOUCHING:
				dictionary_appendsym(tool_dict, gensym("touchZone"), gensym("touching"));
				break;
			default:
				break;
		}
		
		vec = tool.direction();
		atom_setfloat(avec+0, vec.x);
		atom_setfloat(avec+1, vec.y);
		atom_setfloat(avec+2, vec.z);
		dictionary_appendatoms(tool_dict, gensym("direction"), 3, avec);
		
		vec = tool.tipPosition();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(tool_dict, gensym("tipPosition"), 3, avec);
		
		vec = tool.stabilizedTipPosition();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(tool_dict, gensym("stabilizedTipPosition"), 3, avec);
		
		vec = tool.tipVelocity();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(tool_dict, gensym("tipVelocity"), 3, avec);
		
		return tool_dict;
	}
	
	t_dictionary * processHand(const Leap::Hand& hand) {
		t_dictionary * hand_dict = dictionary_new();
		Quaternion q;
		Leap::Vector vec;
		t_atom a[3];
		t_atom avec[4];
		const bool isRight = hand.isRight();
		const int32_t hand_id = hand.id();
		const Leap::Arm &arm = hand.arm();
		long in_savelock;
		
		dictionary_appendlong(hand_dict, _sym_id, hand_id);
		dictionary_appendlong(hand_dict, gensym("frame"), (t_atom_long)hand.frame().id());
		dictionary_appendsym(hand_dict, gensym("hand"), isRight ? gensym("right") : gensym("left"));
		dictionary_appendfloat(hand_dict, gensym("timeVisible"), hand.timeVisible());
		dictionary_appendfloat(hand_dict, gensym("confidence"), hand.confidence());
		dictionary_appendfloat(hand_dict, gensym("grabStrength"), hand.grabStrength()); // open hand (0) to grabbing pose (1)
		dictionary_appendfloat(hand_dict, gensym("pinchStrength"), hand.pinchStrength()); // open hand (0) to pinching pose (1)
		
		t_dictionary * palm_dict = dictionary_new();
		{
			// palm
			vec = hand.direction();
			atom_setfloat(avec+0, vec.x);
			atom_setfloat(avec+1, vec.y);
			atom_setfloat(avec+2, vec.z);
			dictionary_appendatoms(palm_dict, _jit_sym_direction, 3, avec);
			
			vec = hand.palmPosition();
			atom_setfloat(avec+0, vec.x * 0.001);
			atom_setfloat(avec+1, vec.y * 0.001);
			atom_setfloat(avec+2, vec.z * 0.001);
			dictionary_appendatoms(palm_dict, gensym("position"), 3, avec);
			
			vec = hand.stabilizedPalmPosition();
			atom_setfloat(avec+0, vec.x * 0.001);
			atom_setfloat(avec+1, vec.y * 0.001);
			atom_setfloat(avec+2, vec.z * 0.001);
			dictionary_appendatoms(palm_dict, gensym("stabilizedPosition"), 3, avec);
			
			vec = hand.palmNormal();
			atom_setfloat(avec+0, vec.x);
			atom_setfloat(avec+1, vec.y);
			atom_setfloat(avec+2, vec.z);
			dictionary_appendatoms(palm_dict, gensym("normal"), 3, avec);
			
			vec = hand.palmVelocity();
			atom_setfloat(avec+0, vec.x * 0.001);
			atom_setfloat(avec+1, vec.y * 0.001);
			atom_setfloat(avec+2, vec.z * 0.001);
			dictionary_appendatoms(palm_dict, gensym("velocity"), 3, avec);
			
			dictionary_appendfloat(palm_dict, gensym("width"), hand.palmWidth() * 0.001); // in meters
			
			q.fromBasis(hand.basis(), isRight);	// or basis.rigidInverse?
			dictionary_appendatoms(palm_dict, gensym("quat"), 4, q.atoms);
		}
		dictionary_appenddictionary(hand_dict, gensym("palm"), (t_object *)palm_dict);
		
		if (arm.isValid()) {
			t_dictionary * arm_dict = dictionary_new();
			{
				q.fromBasis(arm.basis(), isRight);	// or basis.rigidInverse?
				dictionary_appendatoms(arm_dict, gensym("quat"), 4, q.atoms);
				
				vec = arm.center();
				atom_setfloat(avec+0, vec.x * 0.001);
				atom_setfloat(avec+1, vec.y * 0.001);
				atom_setfloat(avec+2, vec.z * 0.001);
				dictionary_appendatoms(arm_dict, gensym("center"), 3, avec);
				
				vec = arm.elbowPosition();
				atom_setfloat(avec+0, vec.x * 0.001);
				atom_setfloat(avec+1, vec.y * 0.001);
				atom_setfloat(avec+2, vec.z * 0.001);
				dictionary_appendatoms(arm_dict, gensym("elbowPosition"), 3, avec);
				
				Leap::Vector vec1 = arm.wristPosition();
				atom_setfloat(avec+0, vec1.x * 0.001);
				atom_setfloat(avec+1, vec1.y * 0.001);
				atom_setfloat(avec+2, vec1.z * 0.001);
				dictionary_appendatoms(arm_dict, gensym("wristPosition"), 3, avec);
				
				// probably also want length:
				float x1 = vec1.x-vec.x;
				float y1 = vec1.y-vec.y;
				float z1 = vec1.z-vec.z;
				float len = sqrtf(x1*x1+y1*y1+z1*z1);
				
				dictionary_appendfloat(arm_dict, gensym("length"), len * 0.001); // in meters
				dictionary_appendfloat(arm_dict, gensym("width"), arm.width() * 0.001); // in meters
				
				vec = arm.direction();
				atom_setfloat(avec+0, vec.x);
				atom_setfloat(avec+1, vec.y);
				atom_setfloat(avec+2, vec.z);
				dictionary_appendatoms(arm_dict, gensym("direction"), 3, avec);
			}
			dictionary_appenddictionary(hand_dict, gensym("arm"), (t_object *)arm_dict);
		}
		
		{
			// transform since last frame:
			float angle = hand.rotationAngle(lastFrame);
			vec = hand.rotationAxis(lastFrame);
			atom_setfloat(avec+0, angle);
			atom_setfloat(avec+1, vec.x);
			atom_setfloat(avec+2, vec.y);
			atom_setfloat(avec+3, vec.z);
			dictionary_appendatoms(hand_dict, gensym("rotation"), 4, avec);
			dictionary_appendfloat(hand_dict, gensym("rotationProbability"), hand.rotationProbability(lastFrame));
			dictionary_appendfloat(hand_dict, gensym("scaleFactor"), hand.scaleFactor(lastFrame));
			dictionary_appendfloat(hand_dict, gensym("scaleProbability"), hand.scaleProbability(lastFrame));
			
			vec = hand.translation(lastFrame);
			atom_setfloat(avec+0, vec.x * 0.001);
			atom_setfloat(avec+1, vec.y * 0.001);
			atom_setfloat(avec+2, vec.z * 0.001);
			dictionary_appendatoms(hand_dict, gensym("translation"), 3, avec);
			dictionary_appendfloat(hand_dict, gensym("translationProbability"), hand.translationProbability(lastFrame));
		}
		{
			// sphere to fit this hand:
			vec = hand.sphereCenter();
			atom_setfloat(avec+0, vec.x * 0.001);
			atom_setfloat(avec+1, vec.y * 0.001);
			atom_setfloat(avec+2, vec.z * 0.001);
			dictionary_appendatoms(hand_dict, gensym("sphereCenter"), 3, avec);
			dictionary_appendfloat(hand_dict, gensym("sphereRadius"), hand.sphereRadius() * 0.001); // in meters
		}
		
		in_savelock = (long)jit_object_method(hand_mat, _jit_sym_lock, 1);
		jit_object_method(hand_mat, _jit_sym_getdata, &hand_mat_ptr);
		
		// fingers:
		const Leap::FingerList &fingers = hand.fingers();
		t_atom finger_atoms[5];
		for (int i=0; i<5; i++) {
			const Leap::Finger& finger = fingers[i];
			t_symbol * name = 0;
			switch (i) {
				case 0: name = gensym("thumb"); break;
				case 1: name = gensym("index"); break;
				case 2: name = gensym("middle"); break;
				case 3: name = gensym("ring"); break;
				default: name = gensym("pinky"); break;
			}
			t_dictionary * finger_dict = processFinger(fingers[i], isRight, i, name);
			atom_setobj(finger_atoms+i, finger_dict);
		}
		dictionary_appendatoms(hand_dict, gensym("fingers"), 5, finger_atoms);
		
		// restore matrix lock state:
		jit_object_method(hand_mat, _jit_sym_lock, in_savelock);
		
		
		atom_setlong(a+0, hand.id());
		atom_setsym (a+1, _jit_sym_jit_matrix);
		atom_setsym (a+2, jit_attr_getsym(hand_mat_wrapper, _jit_sym_name));
		outlet_anything(outlet_msg, gensym("hand"), 3, a);
		
		const Leap::ToolList& tools = hand.tools();
		size_t numTools = tools.count();
		if (numTools) {
			t_atom tool_atoms[numTools];
			for (size_t i = 0; i<numTools; i++) {
				t_dictionary * tool_dict = processTool(tools[i]);
				
			}
			dictionary_appendatoms(hand_dict, gensym("tools"), numTools, tool_atoms);
		}
		
		return hand_dict;
	}
	
	void processNextFrame(const Leap::Frame& frame, int serialize=0) {
		
		if (!frame.isValid()) return;
		
		t_atom a[2];
		t_atom avec[4];
		Leap::Vector vec;
		Leap::Matrix basis;
		Quaternion q;
		
		// serialize:
		if (serialize) serializeAndOutput(frame);
		
		int64_t frame_id = frame.id();
		
		// The objects returned by the Frame object are all read-only.
		// You can safely store them and use them in the future.
		// They are thread-safe.
		const Leap::HandList hands = frame.hands();
		const Leap::PointableList pointables = frame.pointables();
		const Leap::FingerList fingers = frame.fingers();
		const Leap::ToolList tools = frame.tools();
		
		
		// TODO: Following entities across frames
		// perhaps have an attribute for an ID to track?
		// hand = frame.hand(handID);
		// finger = frame.finger(fingerID) etc.
		
		const size_t numHands = hands.count();
		
		t_atom frame_data[6];
		atom_setlong(frame_data, frame_id);
		atom_setlong(frame_data+1, frame.timestamp());
		atom_setlong(frame_data+2, numHands);
		// front-most hand ID:
		atom_setlong(frame_data+3, frame.hands().frontmost().id());
		atom_setlong(frame_data+4, frame.hands().leftmost().id());
		atom_setlong(frame_data+5, frame.hands().rightmost().id());
		outlet_anything(outlet_frame, ps_frame, 6, frame_data);
		
//		dictionary_appendlong(hand_dict, gensym("fingerFrontmost"), fingers.frontmost().id()); // in meters
//		dictionary_appendlong(hand_dict, gensym("fingerLeftmost"), fingers.leftmost().id()); // in meters
//		dictionary_appendlong(hand_dict, gensym("fingerRightmost"), fingers.rightmost().id()); // in meters
		
		// motion tracking
		// motion tracking data is preceded by a probability vector (Rotate, Scale, Translate)
		if (motion_tracking) {
			t_atom transform[4];
			Leap::Vector vec;
			
			atom_setfloat(transform+0, frame.rotationProbability(lastFrame));
			atom_setfloat(transform+1, frame.scaleProbability(lastFrame));
			atom_setfloat(transform+2, frame.translationProbability(lastFrame));
			outlet_anything(outlet_tracking, ps_probability, 3, frame_data);
			
			vec = frame.rotationAxis(lastFrame);
			atom_setfloat(transform, frame.rotationAngle(lastFrame));
			atom_setfloat(transform+1, vec.x);
			atom_setfloat(transform+2, vec.y);
			atom_setfloat(transform+3, vec.z);
			outlet_anything(outlet_tracking, _jit_sym_rotate, 4, frame_data);
			
			atom_setfloat(transform, frame.scaleFactor(lastFrame));
			outlet_anything(outlet_tracking, _jit_sym_scale, 1, frame_data);
			
			vec = frame.translation(lastFrame);
			atom_setfloat(transform+0, vec.x);
			atom_setfloat(transform+1, vec.y);
			atom_setfloat(transform+2, vec.z);
			outlet_anything(outlet_tracking, _jit_sym_position, 3, frame_data);
		}
		
		
		t_atom hand_atoms[numHands];
		for(size_t i = 0; i < numHands; i++) {
			const Leap::Hand &hand = hands[i];
			if (!hand.isValid()) continue;
			
			t_dictionary * hand_dict = processHand(hand);
			
			t_symbol * name = jit_symbol_unique();
			hand_dict = dictobj_register(hand_dict, &name);
			atom_setsym(a, name);
			outlet_anything(outlet_hands, _sym_dictionary, 1, a);
			object_release((t_object *)hand_dict);
		}
		//dictionary_appendatoms()
		
		
		outlet_anything(outlet_frame, ps_frame_end, 0, NULL);
	}
	
	void getBox() {
		t_atom avec[4];
		t_atom a[1];
		Leap::Vector vec;
		const Leap::InteractionBox& box = frame.interactionBox();
		t_symbol * box_dict_name = jit_symbol_unique();
		t_dictionary * box_dict = dictobj_register(dictionary_new(), &box_dict_name);
		
		vec = box.center();
		atom_setfloat(avec+0, vec.x * 0.001);
		atom_setfloat(avec+1, vec.y * 0.001);
		atom_setfloat(avec+2, vec.z * 0.001);
		dictionary_appendatoms(box_dict, gensym("center"), 3, avec);
		
		vec = box.center();
		atom_setfloat(avec+0, box.width() * 0.001);
		atom_setfloat(avec+1, box.height() * 0.001);
		atom_setfloat(avec+2, box.depth() * 0.001);
		dictionary_appendatoms(box_dict, gensym("size"), 3, avec);
		
		atom_setsym(a, box_dict_name);
		outlet_anything(outlet_msg, gensym("interactionBox"), 1, a);
		object_release((t_object *)box_dict);
	}
	
	// compatibilty with aka.leapmotion:
	void processNextFrameAKA(const Leap::Frame& frame) {
		t_atom a[1];
		t_atom frame_data[3];
		int64_t frame_id = frame.id();
		
        if(frame.isValid()) {
			outlet_anything(outlet_frame, ps_frame_start, 0, NULL);
			
			const Leap::HandList hands = frame.hands();
			const size_t numHands = hands.count();
			
			atom_setlong(frame_data, frame_id);
			atom_setlong(frame_data+1, frame.timestamp());
			atom_setlong(frame_data+2, numHands);
			outlet_anything(outlet_frame, ps_frame, 3, frame_data);
			
			for(size_t i = 0; i < numHands; i++){
				// Hand
				const Leap::Hand &hand = hands[i];
				const int32_t hand_id = hand.id();
				const Leap::FingerList &fingers = hand.fingers();
				bool isRight = hand.isRight();
				
				t_atom hand_data[3];
				atom_setlong(hand_data, hand_id);
				atom_setlong(hand_data+1, frame_id);
				atom_setlong(hand_data+2, fingers.count());
				outlet_anything(outlet_frame, ps_hand, 3, hand_data);
				
				for(size_t j = 0; j < 5; j++) {
					// Finger
					const Leap::Finger &finger = fingers[j];
					const Leap::Finger::Type fingerType = finger.type(); // 0=THUMB ... 4=PINKY
					const int32_t finger_id = finger.id();
					//const Leap::Ray& tip = finger.tip();
					const Leap::Vector direction = finger.direction();
					const Leap::Vector position = finger.tipPosition();
					const Leap::Vector velocity = finger.tipVelocity();
					const double width = finger.width();
					const double lenght = finger.length();
					const bool isTool = finger.isTool();
					
					t_atom finger_data[15];
					atom_setlong(finger_data, finger_id);
					atom_setlong(finger_data+1, hand_id);
					atom_setlong(finger_data+2, frame_id);
					atom_setfloat(finger_data+3, position.x);
					atom_setfloat(finger_data+4, position.y);
					atom_setfloat(finger_data+5, position.z);
					atom_setfloat(finger_data+6, direction.x);
					atom_setfloat(finger_data+7, direction.y);
					atom_setfloat(finger_data+8, direction.z);
					atom_setfloat(finger_data+9, velocity.x);
					atom_setfloat(finger_data+10, velocity.y);
					atom_setfloat(finger_data+11, velocity.z);
					atom_setfloat(finger_data+12, width);
					atom_setfloat(finger_data+13, lenght);
					atom_setlong(finger_data+14, isTool);
					outlet_anything(outlet_frame, ps_finger, 15, finger_data);
				}
				
				const Leap::Vector position = hand.palmPosition();
				const Leap::Vector direction = hand.direction();
				
				t_atom palm_data[14];
				atom_setlong(palm_data, hand_id);
				atom_setlong(palm_data+1, frame_id);
				atom_setfloat(palm_data+2, position.x);
				atom_setfloat(palm_data+3, position.y);
				atom_setfloat(palm_data+4, position.z);
				atom_setfloat(palm_data+5, direction.x);
				atom_setfloat(palm_data+6, direction.y);
				atom_setfloat(palm_data+7, direction.z);
				
				// Palm Velocity
				const Leap::Vector velocity = hand.palmVelocity();
				
				atom_setfloat(palm_data+8, velocity.x);
				atom_setfloat(palm_data+9, velocity.y);
				atom_setfloat(palm_data+10, velocity.z);
				
				// Palm Normal
				const Leap::Vector normal = hand.palmNormal();
				
				atom_setfloat(palm_data+11, normal.x);
				atom_setfloat(palm_data+12, normal.y);
				atom_setfloat(palm_data+13, normal.z);
				outlet_anything(outlet_frame, ps_palm, 14, palm_data);
				
				const Leap::Vector sphereCenter = hand.sphereCenter();
				const double sphereRadius = hand.sphereRadius();
				
				t_atom ball_data[6];
				atom_setlong(ball_data, hand_id);
				atom_setlong(ball_data+1, frame_id);
				atom_setfloat(ball_data+2, sphereCenter.x);
				atom_setfloat(ball_data+3, sphereCenter.y);
				atom_setfloat(ball_data+4, sphereCenter.z);
				atom_setfloat(ball_data+5, sphereRadius);
				outlet_anything(outlet_frame, ps_ball, 6, ball_data);
			}
			outlet_anything(outlet_frame, ps_frame_end, 0, NULL);
		}
	}
	
	void processImageList(const Leap::ImageList& images) {
		t_atom a[5];
		long in_savelock;
		
		// sanity checks:
		if (images.count() < 2) return;
		
		// (re)allocate matrices:
		{
			const Leap::Image& image = images[0];
			if (!image.isValid()) return;
			
			if (image.width() != image_width || image.height() != image_height) {
				image_height = image.height();
				image_width = image.width();
				for (int i=0; i<2; i++) {
					//image_wrappers[i] = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
					image_mats[i] = configureMatrix2D(image_wrappers[i], 1, _jit_sym_char, image_width, image_height);
				}
				object_post(&ob, "IR image dimensions: width %i height %i", image_width, image_height);
				
			}
			
			if (image.distortionWidth()/2 != distortion_dim[0] || image.distortionHeight() != distortion_dim[1]) {
				distortion_dim[0] = image.distortionWidth()/2;
				distortion_dim[1] = image.distortionHeight();
				
				for (int i=0; i<2; i++) {
					//distortion_image_wrappers[i] = jit_object_new(gensym("jit_matrix_wrapper"), jit_symbol_unique(), 0, NULL);
					distortion_image_mats[i] = configureMatrix2D(distortion_image_wrappers[i], 2, _jit_sym_float32, distortion_dim[0], distortion_dim[1]);
				}
				object_post(&ob, "IR calibration image dimensions: width %i height %i", distortion_dim[0], distortion_dim[1]);
			}
		}
	
		for(int i = 0; i < 2; i++){
			const Leap::Image& image = images[i];
			if (image.isValid()) {
				//int64_t id = image.sequenceId(); // like frame.id(); can be used for unique/allframes etc.
				int idx = image.id();
				
				if (image.bytesPerPixel() != 1) {
					post("Leap SDK has changed the image format, so the max object will need to be recompiled...");
					return;
				}
				
				void * mat_wrapper = image_wrappers[idx];
				void * mat = image_mats[idx];
				
				// lock it:
				in_savelock = (long)jit_object_method(mat, _jit_sym_lock, 1);
				{
					// copy into image:
					char * out_bp;
					jit_object_method(mat, _jit_sym_getdata, &out_bp);
					memcpy(out_bp, image.data(), image_width*image_height);
				}
				// restore matrix lock state:
				jit_object_method(mat, _jit_sym_lock, in_savelock);
				
				// output image:
				atom_setsym(a, jit_attr_getsym(mat_wrapper, _jit_sym_name));
				outlet_anything(outlet_image[idx], _jit_sym_jit_matrix, 1, a);
				
				if (distortion_requested) {
					void * mat_wrapper = distortion_image_wrappers[idx];
					void * mat = distortion_image_mats[idx];
					
					// lock it:
					in_savelock = (long)jit_object_method(mat, _jit_sym_lock, 1);
					{
						// copy into image:
						char * out_bp;
						jit_object_method(mat, _jit_sym_getdata, &out_bp);
						memcpy(out_bp, image.distortion(), 2*sizeof(float)*distortion_dim[0]*distortion_dim[1]);
					
//						for (int d = 0; d < distortion_dim[0] * distortion_dim[1]; d += 2) {
//							float dX = distortion_buffer[d];
//							float dY = distortion_buffer[d + 1];
//							if(!((dX < 0) || (dX > 1)) && !((dY < 0) || (dY > 1))) {
//								//Use valid calibration data
//							}
//						}
					}
					// restore matrix lock state:
					jit_object_method(mat, _jit_sym_lock, in_savelock);
					
					// output image:
					atom_setlong(a, idx);
					atom_setsym(a+1, _jit_sym_jit_matrix);
					atom_setsym(a+2, jit_attr_getsym(mat_wrapper, _jit_sym_name));
					outlet_anything(outlet_distortion, gensym("map"), 3, a);
					
					atom_setfloat(a+0, image.rayScaleX());
					atom_setfloat(a+1, image.rayScaleY());
					atom_setfloat(a+2, image.rayOffsetX());
					atom_setfloat(a+3, image.rayOffsetY());
					outlet_anything(outlet_distortion, gensym("scale_and_offset"), 4, a);
					
				}
				
				/*
				 see https://developer.leapmotion.com/documentation/cpp/api/Leap.Image.html#cppclass_leap_1_1_image_1a4c6fa722eba7018e148b13677c7ce609
				 */
			}
		}
		
		distortion_requested = 0;
	}
	
	void processGestures(const Leap::Frame& frame) {
		const Leap::GestureList& gestures = frame.gestures(lastFrame);
		for(Leap::GestureList::const_iterator gl = gestures.begin(); gl != gestures.end(); gl++) {
			if ((*gl).isValid()) {
				dictionary_clear(gesture_dict);
				
				t_atom a[2];
				atom_setsym(a, _sym_dictionary);
				atom_setsym(a+1, gesture_dict_name);
				
				switch ((*gl).state()) {
					case Leap::Gesture::STATE_START:
						dictionary_appendsym(gesture_dict, gensym("state"), gensym("start"));
						break;
					case Leap::Gesture::STATE_UPDATE:
						dictionary_appendsym(gesture_dict, gensym("state"), gensym("update"));
						break;
					case Leap::Gesture::STATE_STOP:
						dictionary_appendsym(gesture_dict, gensym("state"), gensym("stop"));
						break;
					default:
						break;
				}
				
				t_atom avec[3];
				
				switch ((*gl).type()) {
					case Leap::Gesture::TYPE_SWIPE: {
						dictionary_appendsym(gesture_dict, gensym("type"), gensym("swipe"));
						const Leap::SwipeGesture& g = *(gl);
						
						dictionary_appendlong(gesture_dict, _sym_id, g.id());
						
						Leap::Vector position = g.position();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, _jit_sym_position, 3, avec);
						
						Leap::Vector direction = g.direction();
						atom_setfloat(avec, direction.x);
						atom_setfloat(avec+1, direction.y);
						atom_setfloat(avec+2, direction.z);
						dictionary_appendatoms(gesture_dict, _jit_sym_direction, 3, avec);
						
						Leap::HandList hands = g.hands();
						if (hands.count()) dictionary_appendlong(gesture_dict, gensym("hand"), (*hands.begin()).id());
						dictionary_appendlong(gesture_dict, gensym("pointable"), g.pointable().id());
						
						position = g.startPosition();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, gensym("startPosition"), 3, avec);
						dictionary_appendfloat(gesture_dict, gensym("duration"), g.durationSeconds());
						dictionary_appendfloat(gesture_dict, gensym("speed"), g.speed());
						
						outlet_anything(outlet_gesture, gensym("swipe"), 2, a);
						
					} break;
					case Leap::Gesture::TYPE_CIRCLE: {
						dictionary_appendsym(gesture_dict, gensym("type"), gensym("circle"));
						const Leap::CircleGesture& g = *(gl);
						
						dictionary_appendlong(gesture_dict, _sym_id, g.id());
						
						Leap::Vector position = g.center();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, gensym("center"), 3, avec);
						
						position = g.normal();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, gensym("normal"), 3, avec);
						
						Leap::HandList hands = g.hands();
						if (hands.count()) dictionary_appendlong(gesture_dict, gensym("hand"), (*hands.begin()).id());
						dictionary_appendlong(gesture_dict, gensym("pointable"), g.pointable().id());
						dictionary_appendfloat(gesture_dict, gensym("duration"), g.durationSeconds());
						dictionary_appendfloat(gesture_dict, gensym("progress"), g.progress());
						dictionary_appendfloat(gesture_dict, gensym("radius"), g.radius());
						
						outlet_anything(outlet_gesture, gensym("circle"), 2, a);
						
					} break;
					case Leap::Gesture::TYPE_KEY_TAP: {
						dictionary_appendsym(gesture_dict, gensym("type"), gensym("key_tap"));
						const Leap::KeyTapGesture& g = *(gl);
						
						dictionary_appendlong(gesture_dict, _sym_id, g.id());
						
						Leap::Vector position = g.position();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, _jit_sym_position, 3, avec);
						
						position = g.direction();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, gensym("direction"), 3, avec);
						
						Leap::HandList hands = g.hands();
						if (hands.count()) dictionary_appendlong(gesture_dict, gensym("hand"), (*hands.begin()).id());
						dictionary_appendlong(gesture_dict, gensym("pointable"), g.pointable().id());
						dictionary_appendfloat(gesture_dict, gensym("duration"), g.durationSeconds());
						
						outlet_anything(outlet_gesture, gensym("key_tap"), 2, a);
					} break;
					case Leap::Gesture::TYPE_SCREEN_TAP: {
						dictionary_appendsym(gesture_dict, gensym("type"), gensym("screen_tap"));
						const Leap::ScreenTapGesture& g = *(gl);
						
						dictionary_appendlong(gesture_dict, _sym_id, g.id());
						
						Leap::Vector position = g.position();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, _jit_sym_position, 3, avec);
						
						position = g.direction();
						atom_setfloat(avec, position.x);
						atom_setfloat(avec+1, position.y);
						atom_setfloat(avec+2, position.z);
						dictionary_appendatoms(gesture_dict, gensym("direction"), 3, avec);
						
						Leap::HandList hands = g.hands();
						if (hands.count()) dictionary_appendlong(gesture_dict, gensym("hand"), (*hands.begin()).id());
						dictionary_appendlong(gesture_dict, gensym("pointable"), g.pointable().id());
						dictionary_appendfloat(gesture_dict, gensym("duration"), g.durationSeconds());
						
						outlet_anything(outlet_gesture, gensym("screen_tap"), 2, a);
					} break;
					default: {
						//Handle unrecognized gestures?
					} break;
				}
			}
		}
	}
	
    void bang() {
		t_atom a[1];
		atom_setlong(a, controller.isConnected());
		outlet_anything(outlet_msg, ps_connected, 1, a);
		
		if(!controller.isConnected()) return;
			
		Leap::Frame frame = controller.frame();
		float fps = frame.currentFramesPerSecond();
		atom_setfloat(a, fps);
		outlet_anything(outlet_msg, ps_fps, 1, a);
		
		int64_t currentID = frame.id();
		if ((!unique) || currentID > lastFrameID) {		// is this frame new?
			if (allframes) {
				// output all pending frames:
				for (int history = 0; history < currentID - lastFrameID; history++) {
					// important that we re-use the frame variable here:
					frame = controller.frame(history);
					if (images) {
						// get most recent images:
						processImageList(frame.images());
					}				
					if (aka) {
						processNextFrameAKA(frame);
					} else {
						processNextFrame(frame, serialize);
					}
				}
			} else {
				if (images) {
					// get most recent images:
					processImageList(controller.images());
				}				
				// The latest frame only
				if (aka) {
					processNextFrameAKA(frame);
				} else {
					processNextFrame(frame, serialize);
				}
			}
		}
		
		processGestures(frame);
		
		lastFrame = frame;
		lastFrameID = currentID;
    }
	
	void jit_matrix(t_symbol * name) {
		int len;
		t_jit_matrix_info in_info;
		long in_savelock;
		SerializedFrame * in_bp;
		t_jit_err err = 0;
		Leap::Frame frame;
		
		// get matrix from name:
		void * in_mat = jit_object_findregistered(name);
		if (!in_mat) {
			object_error(&ob, "failed to acquire matrix");
			err = JIT_ERR_INVALID_INPUT;
			goto out;
		}
		
		// lock it:
		in_savelock = (long)jit_object_method(in_mat, _jit_sym_lock, 1);
		
		// first ensure the type is correct:
		jit_object_method(in_mat, _jit_sym_getinfo, &in_info);
		jit_object_method(in_mat, _jit_sym_getdata, &in_bp);
		if (!in_bp) {
			err = JIT_ERR_INVALID_INPUT;
			goto unlock;
		}
		if (in_info.planecount != 1) {
			err = JIT_ERR_MISMATCH_PLANE;
			goto unlock;
		}
		if (in_info.type != _jit_sym_char) {
			err = JIT_ERR_MISMATCH_TYPE;
			goto unlock;
		}
		if (in_info.dimcount != 1) {
			err = JIT_ERR_MISMATCH_DIM;
			goto unlock;
		}
		
		frame.deserialize(in_bp->data, in_bp->length);
		//frame.deserialize(in_bp->data, in_bp->length);
		if (aka) {
			processNextFrameAKA(frame);
		} else {
			processNextFrame(frame, 0);
		}
		
	unlock:
		// restore matrix lock state:
		jit_object_method(in_mat, _jit_sym_lock, in_savelock);
	out:
		if (err) {
			jit_error_code(&ob, err);
		}
	}
	
	t_jit_err dictionary(t_symbol *s) {
		t_dictionary *d = dictobj_findregistered_retain(s);
		if (d) {
			dictionary_clone_to_existing(d,config_dict);
		} else {
			object_error(&ob, "unable to reference dictionary named %s", s->s_name);
			return JIT_ERR_GENERIC;
		}
		dictobj_release(d);
		return JIT_ERR_NONE;
	}
};

//t_max_err leap_notify(t_leap *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
//    t_symbol *attrname;
//    if (msg == _sym_attr_modified) {       // check notification type
//        attrname = (t_symbol *)object_method((t_object *)data, _sym_getname);
//        object_post((t_object *)x, "changed attr name is %s", attrname->s_name);
//    } else {
//        object_post((t_object *)x, "notify %s (self %d)", msg->s_name, sender == x);
//    }
//    return 0;
//}

void leap_bang(t_leap * x) {
    x->bang();
}

void leap_getbox(t_leap * x) {
	x->getBox();
}

void leap_getdistortion(t_leap * x) {
	x->distortion_requested = 1;
}

void leap_jit_matrix(t_leap *x, t_symbol * s) {
	x->jit_matrix(s);
}

void leap_doconfigure(t_leap *x) {
    x->configure();
}

void leap_configure(t_leap *x) {
    defer_low(x, (method)leap_doconfigure, 0, 0, 0);
}

void leap_assist(t_leap *x, void *b, long m, long a, char *s)
{
    if (m == ASSIST_INLET) { // inlet
        if (a == 0) {
            sprintf(s, "bang to report frame data");
        } else {
            sprintf(s, "I am inlet %ld", a);
        }
    } else {	// outlet
        if (a == 0) {
            sprintf(s, "frame data (messages)");
        } else if (a == 1) {
            sprintf(s, "image (left)");
        } else if (a == 2) {
			sprintf(s, "image (right)");
		} else if (a == 3) {
			sprintf(s, "recognized hands (dict)");
        } else if (a == 4) {
			sprintf(s, "recognized gestures (messages)");
		} else if (a == 5) {
			sprintf(s, "motion tracking data (messages)");
		} else if (a == 6) {
			sprintf(s, "IR distortion calibration data (messages)");
//        } else if (a == 4) {
//            sprintf(s, "HMD left eye mesh (jit_matrix)");
//        } else if (a == 5) {
//            sprintf(s, "HMD right eye properties (messages)");
//        } else if (a == 6) {
//            sprintf(s, "HMD right eye mesh (jit_matrix)");
//        } else if (a == 7) {
//            sprintf(s, "HMD properties (messages)");
        } else {
            sprintf(s, "other messages");
			//sprintf(s, "I am outlet %ld", a);
        }
    }
}

t_max_err leap_notify(t_leap *x, t_symbol *s, t_symbol *msg, void *sender, void *data) {
	t_symbol *attrname;
	if (msg == gensym("attr_modified")) {       // check notification type
		attrname = (t_symbol *)object_method((t_object *)data, gensym("getname"));
		
		// certain attributes require a reconfigure:
		if (attrname == gensym("hmd") ||
			attrname == gensym("background") ||
			attrname == gensym("config") ||
			attrname == gensym("images") ||
			attrname == gensym("gesture_swipe") ||
			attrname == gensym("gesture_circle") ||
			attrname == gensym("gesture_key_tap") ||
			attrname == gensym("gesture_screen_tap") ||
			attrname == gensym("gesture_any")) {
			x->configure();
		}
		
		//object_post((t_object *)x, "changed attr name is %s",attrname->s_name);
	} else {
		//object_post((t_object *)x, "notify %s (self %d)", msg->s_name, sender == x);
	}
	return 0;
}


void leap_free(t_leap *x) {
	object_unregister(x);
    x->~t_leap();
    max_jit_object_free(x);
}

void *leap_new(t_symbol *s, long argc, t_atom *argv)
{
    t_leap *x = NULL;
    if ((x = (t_leap *)object_alloc(leap_class))) {
		// initialize in-place:
        x = new (x) t_leap();
		
		// register, in order to receive notifications from oneself:
		object_register(gensym("leap"), jit_symbol_unique(), x);
		object_attach_byptr(x, x);
		
        // apply attrs:
        attr_args_process(x, argc, argv);
    }
    return (x);
}

int C74_EXPORT main(void) {	
    t_class *maxclass;

#ifdef WIN_VERSION
	// Rather annoyingly, leap don't provide static libraries, only a Leap.dll
	// which should sit next to Max.exe -- not very user friendly for Max.
	// So instead we delay load the library from next to the leap.mxe
	{
		char path[MAX_PATH];
		char mxepath[MAX_PATH];
		char dllpath[MAX_PATH];
		const char * name = "Leap.dll";
		// get leap.mxe as a HMODULE:
		HMODULE hm = NULL;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) &leap_class, &hm)) {
			error("critical error loading leap.mxe: GetModuleHandle returned %d\n", (int)GetLastError());
			return 0;
		}
		// get its full path, the containing directory, and then munge the desired dllpath from it:
		GetModuleFileNameA(hm, path, sizeof(path));
		_splitpath(path, NULL, mxepath, NULL, NULL);
		_snprintf(dllpath, MAX_PATH, "%s%s", mxepath, name);
		// now try to load the dll:
		hm = LoadLibrary(dllpath);
		if (hm == NULL) {
			error("failed to load %s at %s", name, dllpath);
			post("please make sure that %s is located next to leap.mxe", name);
			return 0;
		}
	}
#endif

	common_symbols_init();
	ps_frame_start = gensym("frame_start");
	ps_frame_end = gensym("frame_end");
	ps_frame = gensym("frame");
	ps_hand = gensym("hand");
	ps_finger = gensym("finger");
	ps_palm = gensym("palm");
	ps_ball = gensym("ball");
	ps_fps = gensym("fps");
	ps_connected = gensym("connected");
	ps_probability = gensym("probability");

	maxclass = class_new("leap", (method)leap_new, (method)leap_free, (long)sizeof(t_leap), 0L, A_GIMME, 0);

	class_addmethod(maxclass, (method)leap_assist, "assist", A_CANT, 0);
	class_addmethod(maxclass, (method)leap_notify, "notify", A_CANT, 0);

	class_addmethod(maxclass, (method)leap_jit_matrix, "jit_matrix", A_SYM, 0);
	class_addmethod(maxclass, (method)leap_bang, "bang", 0);
	class_addmethod(maxclass, (method)leap_bang, "getbox", 0);
	class_addmethod(maxclass, (method)leap_getdistortion, "getdistortion", 0);
	class_addmethod(maxclass, (method)leap_configure, "configure", 0);

	CLASS_ATTR_SYM(maxclass, "config", 0, t_leap, config);

	CLASS_ATTR_LONG(maxclass, "unique", 0, t_leap, unique);
	CLASS_ATTR_STYLE_LABEL(maxclass, "unique", 0, "onoff", "unique: output only new frames");

	CLASS_ATTR_LONG(maxclass, "allframes", 0, t_leap, allframes);
	CLASS_ATTR_STYLE_LABEL(maxclass, "allframes", 0, "onoff", "allframes: output all frames between each bang");

	CLASS_ATTR_LONG(maxclass, "images", 0, t_leap, images);
	CLASS_ATTR_STYLE_LABEL(maxclass, "images", 0, "onoff", "images: output raw IR images from the sensor");

	CLASS_ATTR_LONG(maxclass, "hmd", 0, t_leap, hmd);
	CLASS_ATTR_STYLE_LABEL(maxclass, "hmd", 0, "onoff", "hmd: enable to optimize for head-mounted display (LeapVR)");

	CLASS_ATTR_LONG(maxclass, "background", 0, t_leap, background);
	CLASS_ATTR_STYLE_LABEL(maxclass, "background", 0, "onoff", "background: enable data capture when app has lost focus");

	CLASS_ATTR_LONG(maxclass, "motion_tracking", 0, t_leap, motion_tracking);
	CLASS_ATTR_STYLE_LABEL(maxclass, "motion_tracking", 0, "onoff", "motion_tracking: output estimated rotation/scale/translation between polls");

	CLASS_ATTR_LONG(maxclass, "serialize", 0, t_leap, serialize);
	CLASS_ATTR_STYLE_LABEL(maxclass, "serialize", 0, "onoff", "serialize: output serialized frames");

	CLASS_ATTR_LONG(maxclass, "aka", 0, t_leap, aka);
	CLASS_ATTR_STYLE_LABEL(maxclass, "aka", 0, "onoff", "aka: provide output compatible with aka.leapmotion");

	CLASS_ATTR_LONG(maxclass, "gesture_swipe", 0, t_leap, gesture_swipe);
	CLASS_ATTR_STYLE_LABEL(maxclass, "gesture_swipe", 0, "onoff", "gesture_swipe: recognize a long, linear movement of a finger");
	CLASS_ATTR_LONG(maxclass, "gesture_circle", 0, t_leap, gesture_circle);
	CLASS_ATTR_STYLE_LABEL(maxclass, "gesture_circle", 0, "onoff", "gesture_circle: recognize a single finger tracing a circle");
	CLASS_ATTR_LONG(maxclass, "gesture_screen_tap", 0, t_leap, gesture_screen_tap);
	CLASS_ATTR_STYLE_LABEL(maxclass, "gesture_screen_tap", 0, "onoff", "gesture_screen_tap: recognize a tapping movement by the finger as if tapping a vertical computer screen.");
	CLASS_ATTR_LONG(maxclass, "gesture_key_tap", 0, t_leap, gesture_key_tap);
	CLASS_ATTR_STYLE_LABEL(maxclass, "gesture_key_tap", 0, "onoff", "gesture_key_tap: recognize a tapping movement by a finger as if tapping a keyboard key");

	CLASS_ATTR_LONG(maxclass, "gesture_any", 0, t_leap, gesture_any);
	CLASS_ATTR_STYLE_LABEL(maxclass, "gesture_any", 0, "onoff", "gesture_any: if enabled, all gestures are recognized.");


	class_register(CLASS_BOX, maxclass); 
	leap_class = maxclass;
	return 0;
}
