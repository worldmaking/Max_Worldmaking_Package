
#include "al_max.h"
#include "buffer.h"
#include "ext_atomic.h"

#ifdef WIN_VERSION
#define AL_WIN
#include "Shlwapi.h"
#else
#define AL_OSX
#endif

// TODO: how many of these are really needed?
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef AL_WIN
const char * GetLastErrorAsString() {
	static char buf[256];
	FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				   NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				   buf, sizeof(buf), NULL);
	return buf;
}
#endif
#ifdef AL_OSX
#include <unistd.h>
#include <sys/mman.h>
#endif

//#include "al_mmap.h"

static t_class * max_class = 0;
t_symbol *ps_nothing, *ps_buffer, *ps_dirty;

class Bin2Buf {
public:
	t_pxobject ob;
	
	void * outlet_msg;
	
	// the memory-mapped data:
	t_symbol * filename;
	size_t sharesize;
	float * shared;
	
	// the buffer to copy to:
	t_symbol * bufname;
	
	// the memory map share:
#ifdef AL_WIN
	HANDLE mmap_handle = 0;
#endif
#ifdef AL_OSX
	int fd = -1;
#endif
	
	Bin2Buf() {
		outlet_msg = outlet_new(&ob, NULL);
		shared = 0;
		sharesize = 0;
		bufname = 0;
		filename = 0;
		
		// input signals:
		dsp_setup(&ob, 1);
		
	}
	
	void init() {
		shared = read();
	}
	
	void release() {
#ifdef AL_WIN
		if (mmap_handle) {
			UnmapViewOfFile(shared);
			shared = 0;
			sharesize = 0;
			CloseHandle(mmap_handle);
			mmap_handle = 0;
		}
#endif
#ifdef AL_OSX
		// Don't forget to free the mmapped memory
		if (shared) {
			if (munmap(shared, sharesize) == -1) {
				object_error((t_object *)this, "error un-mmapping the file");
			}
			shared = 0;
			sharesize = 0;
		}
		if (fd > 0) {
			close(fd);
			fd = -1;
		}
#endif
	}
	
	float * read() {
		if (shared) return shared;

		if (!filename) return 0;
		
		release();
		
#ifdef AL_WIN
		
		/*
		if (!PathFileExistsA(filename->s_name)) {
			object_error((t_object *)this, "File %s does not exist", filename->s_name);
			return 0;
		}*/
	
		HANDLE file = CreateFileA(filename->s_name,
								  GENERIC_READ, // what I want to do with it
								  FILE_SHARE_READ | FILE_SHARE_WRITE, // what I want to allow others to do with it
								  NULL, // change this to allow child processes to inherit the handle
								  OPEN_EXISTING,
								  FILE_ATTRIBUTE_NORMAL, // any special attributes
								  NULL
								  );
		if (file == INVALID_HANDLE_VALUE) {
			object_error((t_object *)this, "Error opening file %s: %s", filename->s_name, GetLastErrorAsString());
			return 0;
		}
		
		sharesize = GetFileSize(file, NULL);
		
		mmap_handle = CreateFileMappingA(file,
										 NULL, // change this to allow child processes to inherit the handle
										 PAGE_READONLY, // what I want to do with it
										 0, sharesize, // size to map
										 NULL // name
										 );
		//mmap_handle = OpenFileMappingA(FILE_MAP_READ, FALSE, path);
		if (!mmap_handle) {
			object_error((t_object *)this, "Error mapping file %s: %s", filename->s_name, GetLastErrorAsString());
			CloseHandle(file);
			return 0;
		}
		
		shared = (float *)MapViewOfFile(mmap_handle, FILE_MAP_READ, 0, 0, sharesize);
		if (!shared) {
			CloseHandle(file);
			CloseHandle(mmap_handle);
			mmap_handle = NULL;
			object_error((t_object *)this, "Error mapping view of file %s: %s", filename->s_name, GetLastErrorAsString());
			return 0;
		}
#endif
		
#ifdef AL_OSX
		// open
		fd = open(filename->s_name, O_RDONLY, 0666); // 0666 or 0644 or 0600?
		if (fd == -1) {
			object_error((t_object *)this, "Error opening file for reading");
			return 0;
		}
		// get size
		struct stat fileInfo = {0};
		if (fstat(fd, &fileInfo) == -1) {
			object_error((t_object *)this, "Error getting the file size");
			return 0;
		}
		if (fileInfo.st_size == 0) {
			object_error((t_object *)this, "file %s is empty", filename->s_name);
			return 0;
		}
		
		sharesize = fileInfo.st_size;
		
		auto flag = PROT_READ;
		shared = (float *)mmap(0, sharesize, flag, MAP_SHARED, fd, 0);
		if (shared == MAP_FAILED) {
			close(fd);
			object_error((t_object *)this, "mmapping the file");
			return 0;
		}
		
#endif
		
		object_post((t_object *)this, "mapped file %s of size %d", filename->s_name, sharesize);
		return shared;
	}
	
	~Bin2Buf() {
		release();
	}
	
	void dsp64(t_object *dsp64, short *count, double samplerate, long framesize, long flags) {
		
		// connect to MSP dsp chain:
		long options = 0;
		object_method(dsp64, gensym("dsp_add64"), this, static_perform64, options, 0);
	}
	
	void perform64(t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags) {
	
		if (!shared || !sharesize
			|| !bufname || bufname == ps_nothing) return;
		t_buffer * b = (t_buffer *)bufname->s_thing;
		if (!b || ob_sym(b) != ps_buffer) return;
		
		
		
		ATOMIC_INCREMENT(&b->b_inuse);
		if (b->b_valid) {
			int nframes = b->b_frames;
			int nchans = b->b_nchans;
			float * dest = b->b_samples;
			// no. frames available in the bin:
			int shareframes = sharesize / (sizeof(float) * nchans);
			{
				t_double * src = ins[0];
				
				int n = sampleframes;
				while (n--) {
					int idx = (*src++);
					
					// get offset into shared:
					float * const frame = shared + ((idx % shareframes) * nchans);
					float * bframe = dest + ((idx % nframes)*nchans);
					
					for (int c=0; c<nchans; c++) {
						bframe[c] = frame[c];
					}
					
				}
			}
			dirtybuffer(b);
		}
		ATOMIC_DECREMENT(&b->b_inuse);
		
	}
	
	static void static_perform64(Bin2Buf *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long sampleframes, long flags, void *userparam) {
		x->perform64(dsp64, ins, numins, outs, numouts, sampleframes, flags);
	}
	
	void dirtybuffer(void *b) {
		static zero_meth dirtier = 0;
		static bool dirtylookup = false;
		
		if (!dirtylookup) {
			dirtier = (zero_meth)zgetfn((t_object *)b, ps_dirty);
			dirtylookup = true;
		}
		if (dirtier) (*dirtier)(b);
	}
};

void * bin2buf_new(t_symbol *s, long argc, t_atom *argv) {
	Bin2Buf *self = NULL;
	if ((self = (Bin2Buf *)object_alloc(max_class))) {
		
		self = new (self) Bin2Buf();
		
		// apply attrs:
		attr_args_process(self, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		self->init();
		
	}
	return self;
}

void bin2buf_free(Bin2Buf *self) {
	self->~Bin2Buf();
}

void bin2buf_assist(Bin2Buf *self, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "messages in");
	}
	else {
		switch(a) {
			case 0: sprintf(s, "query results"); break;
			default:sprintf(s, "messages"); break;
		}
	}
}

// registers a function for the signal chain in Max
static void bin2buf_dsp64(Bin2Buf *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags) {
	x->dsp64(dsp64, count, samplerate, maxvectorsize, flags);
}

// this lets us double-click on index~ to open up the buffer~ it references
void bin2buf_dblclick(Bin2Buf *x) {
	//buffer_view(buffer_ref_getobject(x->buffer_reference));
	
	void *b;
	if (x->bufname && (b = (x->bufname->s_thing)) && ob_sym(b) == ps_buffer)
		mess0((t_object *)b,gensym("dblclick"));
}

t_max_err bin2buf_file_set(Bin2Buf *x, t_object *attr, long argc, t_atom *argv) {
	
	short outvol;
	t_fourcc outtype;
	t_symbol * filename = atom_getsym(argv);
	char folderpath[MAX_FILENAME_CHARS];
	char systempath[MAX_FILENAME_CHARS];
	
	post("look for %s", filename->s_name);
	
	short result = locatefile_extended(filename->s_name, &outvol, &outtype, NULL, 0);
	if (result == 0) {
		if (path_toabsolutesystempath(outvol, filename->s_name, folderpath) == 0) {
			if (path_nameconform(folderpath, systempath, PATH_STYLE_NATIVE_PLAT, PATH_TYPE_BOOT) == 0) {
				
				t_symbol * newpath = gensym(systempath);

				if (newpath != x->filename) {
					x->filename = newpath;
					object_post((t_object *)x, "loading %s", x->filename->s_name);

					x->init();
				}
			} else {
				object_error((t_object *)x, "couldn't conform path for %s", filename->s_name);
			}
		} else {
			object_error((t_object *)x, "couldn't get absolute path for %s", filename->s_name);
		}
	} else {
		object_error((t_object *)x, "couldn't find %s", filename->s_name);
	}
	return MAX_ERR_NONE;
}


extern "C" void ext_main(void *r)
{
	t_class *c;
	
	ps_nothing = gensym("");
	ps_buffer = gensym("buffer~");
	ps_dirty = gensym("dirty");

	c = class_new("bin2buf~", (method)bin2buf_new, (method)bin2buf_free, (long)sizeof(Bin2Buf), 0L, A_GIMME, 0);
	
	class_addmethod(c, (method)bin2buf_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)bin2buf_dsp64, "dsp64", A_CANT, 0);
	
	//CLASS_ATTR_LONG(c, "port", 0, Bin2Buf, port);
	//CLASS_ATTR_SYM(c, "host", 0, Bin2Buf, host);
	//CLASS_ATTR_ACCESSORS(c, "host", NULL, bin2buf_host_set);
	//CLASS_ATTR_ACCESSORS(c, "name", NULL, bin2buf_name_set);
	
	CLASS_ATTR_SYM(c, "buffer", 0, Bin2Buf, bufname);
	CLASS_ATTR_SYM(c, "file", 0, Bin2Buf, filename);
	CLASS_ATTR_ACCESSORS(c, "file", NULL, bin2buf_file_set);
	
	class_dspinit(c);
	class_register(CLASS_BOX, c);
	max_class = c;
}
