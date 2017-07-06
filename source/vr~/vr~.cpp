#include "al_max.h"
#include "al_math.h"

/*
 Steam Audio API
 
 */
#include "steamaudio_api_2.0-beta.6/include/phonon.h"

#include <new> // for in-place constructor
#include <vector>

static t_class * max_class = 0;

inline float mix(float x, float y, float a) { return x + a*(y-x); }

class VRMSP {
public:
	t_pxobject ob; // max objExamplet, must be first!
	
	// attr
	t_atom_long interp = 1;
	t_atom_long dynamic = 1;
	glm::vec3 direction;
	
	struct {
		IPLContext context;
		IPLRenderingSettings settings;
		IPLHrtfParams hrtfParams;
		IPLAudioFormat source_format;
		IPLAudioFormat output_format;
		
		IPLhandle renderer = 0;
		IPLhandle binaural = 0;
		
		// pre-allocated to maximum vector size, in case this is cheaper?
		IPLfloat32 source_buffer[4096];
		IPLfloat32 output_buffer[4096 * 2];
	} phonon;
	
	VRMSP() {
		
		dsp_setup(&ob, 1);	// MSP inlets: arg is # of inlets and is REQUIRED!
	
		// stereo output:
		outlet_new(&ob, "signal");
		outlet_new(&ob, "signal");
		
		// default position in front of listener, to avoid 0,0,0
		direction.x = 0;
		direction.y = 0;
		direction.z = -1;
		
		// TODO: consider mapping to sysmem handers?
		phonon.context.allocateCallback = 0;
		phonon.context.freeCallback = 0;
		
		phonon.context.logCallback = phonon_log_function;
		// use default convolution setting, as the alternative depends on AMD gpus
		phonon.settings.convolutionType = IPL_CONVOLUTIONTYPE_PHONON;
		
		// various options:
		phonon.hrtfParams.type = IPL_HRTFDATABASETYPE_DEFAULT; // or CUSTIOM
		phonon.hrtfParams.hrtfData = 0;	// Reserved. Must be NULL.
		// TODO: allow custom HRTFs; implement these:
		phonon.hrtfParams.numHrirSamples = 0;
		phonon.hrtfParams.loadCallback = 0;
		phonon.hrtfParams.unloadCallback = 0;
		phonon.hrtfParams.lookupCallback = 0;
		
	}

	static void phonon_log_function(char* message) {
		object_post(0, message);
	}

	~VRMSP() {
		phonon_cleanup();
	}
	
	void phonon_cleanup() {
		
		if (phonon.renderer) iplDestroyBinauralRenderer(&phonon.renderer);
		if (phonon.binaural) iplDestroyBinauralEffect(&phonon.binaural);
	}

	void dsp64(t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
		if (samplerate != 24000 && samplerate != 44100 && samplerate != 48000) {
			object_error((t_object *)this, "unsupported samplerate %f; only 24000 Hz, 44100 Hz, and 48000 Hz are supported", samplerate);
			return;
		}
		if (maxvectorsize > 4096) {
			object_error((t_object *)this, "unsupported vectorsize %d; up to 4096 supported", maxvectorsize);
			return;
		}
		
		// reset:
		phonon_cleanup();
		
		phonon.settings.samplingRate = samplerate;
		phonon.settings.frameSize = maxvectorsize;
		
		object_post((t_object *)this, "sr %f vs %d", samplerate, maxvectorsize);
		
		// create renderer with these settings:
		phonon_cleanup();
		
		iplCreateBinauralRenderer(phonon.context, phonon.settings, phonon.hrtfParams, &phonon.renderer);
		
		// a single mono source
		phonon.source_format.channelLayoutType  = IPL_CHANNELLAYOUTTYPE_SPEAKERS;
		phonon.source_format.channelLayout      = IPL_CHANNELLAYOUT_MONO;
		phonon.source_format.numSpeakers		= 1;
		phonon.source_format.channelOrder       = IPL_CHANNELORDER_INTERLEAVED;
		
		// TODO: for ambi sources,
		// .channelLayoutType = IPL_CHANNELLAYOUTTYPE_AMBISONICS
		// set .ambisonicsOrder, .ambisonicsOrdering, .ambisonicsNormalization
		
		phonon.output_format.channelLayoutType  = IPL_CHANNELLAYOUTTYPE_SPEAKERS;
		phonon.output_format.channelLayout      = IPL_CHANNELLAYOUT_STEREO;
		phonon.output_format.numSpeakers		= 2;
		phonon.output_format.channelOrder       = IPL_CHANNELORDER_INTERLEAVED;
		
		// TODO: for non-binaural output, set .channelLayout = CUSTOM and set .speakerDirections
		
		
		// create binaural effect:
		iplCreateBinauralEffect(phonon.renderer, phonon.source_format, phonon.output_format, &phonon.binaural);
		
		// connect to MSP dsp chain:
		long options = 0;
		object_method(dsp64, gensym("dsp_add64"), this, vrmsp_perform64, options, 0);
	}
	
	static void vrmsp_perform64(VRMSP *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam) {
		x->perform64(dsp64, ins, numins, outs, numouts, sampleframes, flags);
	}
	
	void perform64(t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags) {
		
		static int once = 1;
		if (once) {
			once = 0;
			object_post((t_object *)this, "dsp vs %d", sampleframes);
		}
		
		// phonon uses float32 processing, so we need to copy :-(
		
		IPLAudioBuffer outbuffer;
		outbuffer.format = phonon.output_format;
		outbuffer.numSamples = sampleframes;
		outbuffer.interleavedBuffer = phonon.output_buffer;
		
		IPLAudioBuffer inbuffer;
		inbuffer.format = phonon.source_format;
		inbuffer.numSamples = sampleframes;
		inbuffer.interleavedBuffer = phonon.source_buffer;
		
		// copy input:
		{
			t_double * src = ins[0];
			IPLfloat32 * dst = phonon.source_buffer;
			int n = sampleframes;
			while (n--) { *dst++ = *src++; }
		}
		
		// Unit vector from the listener to the point source,
		// relative to the listener's coordinate system.
		// TODO: apply head tracking quat transform to direction
		glm::vec3 dirn = glm::normalize(direction);
		
		// rotate at 3 hz:
		static float t = 0.f;
		t += M_PI * 2. * 3. * sampleframes/(44100.);
		IPLVector3 dir;
		dir.x = sin(t);
		dir.y = 0.;
		dir.z = cos(t);

		// Note:
		// IPL_HRTFINTERPOLATION_BILINEAR has high CPU cost
		// Typically, bilinear filtering is most useful for wide-band noise-like sounds, such as radio static, mechanical noise, fire, etc.
		// Must use IPL_HRTFINTERPOLATION_BILINEAR if using a custom HRTF
		
		{
			IPLAudioBuffer outbuffer;
			outbuffer.format = phonon.output_format;
			outbuffer.numSamples = sampleframes;
			outbuffer.interleavedBuffer = phonon.output_buffer;
			iplApplyBinauralEffect(phonon.binaural,
								   inbuffer,
								   dir, //*(IPLVector3 *)(&dirn.x),
								   interp ? IPL_HRTFINTERPOLATION_BILINEAR : IPL_HRTFINTERPOLATION_NEAREST,
								   outbuffer);
		}
		
		
		// copy output:
		{
			IPLfloat32 * src = phonon.output_buffer;
			t_double * dst0 = outs[0];
			t_double * dst1 = outs[1];
			int n = sampleframes;
			while (n--) {
				*dst0++ = *src++;
				*dst1++ = *src++;
			}
		}
	}
};

void * vrmsp_new(t_symbol *s, long argc, t_atom *argv) {
	VRMSP *x = NULL;
	if ((x = (VRMSP *)object_alloc(max_class))) {
		
		x = new (x) VRMSP();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		
	}
	return (x);
}

void vrmsp_free(VRMSP *x) {
	x->~VRMSP();
}

// registers a function for the signal chain in Max
void vrmsp_dsp64(VRMSP *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
	x->dsp64(dsp64, count, samplerate, maxvectorsize, flags);
}

void vrmsp_assist(VRMSP *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	}
	else {	// outlet
		sprintf(s, "I am outlet %ld", a);
	}
}

void ext_main(void *r)
{
	t_class *c;
	
	
	c = class_new("vr~", (method)vrmsp_new, (method)vrmsp_free, (long)sizeof(VRMSP),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)vrmsp_assist,			"assist",		A_CANT, 0);
	class_addmethod(c, (method)vrmsp_dsp64,		"dsp64",	A_CANT, 0);
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	
	CLASS_ATTR_LONG(c, "interp", 0, VRMSP, interp);
	CLASS_ATTR_STYLE(c, "interp", 0, "onoff");
	CLASS_ATTR_LONG(c, "dynamic", 0, VRMSP, dynamic);
	CLASS_ATTR_STYLE(c, "dynamic", 0, "onoff");
	
	CLASS_ATTR_FLOAT_ARRAY(c, "direction", 0, VRMSP, direction, 3);
	
	max_class = c;
}
