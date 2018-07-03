#ifndef AL_HASHSPACE_H
#define AL_HASHSPACE_H

#include <vector>

#include "al_math.h"

/*
	A POD-friendly data structure for near-neighbour queries
	
	Stores object unique ids (int32_t) into a voxel grid
	There are up to MAX_OBJECTS objects in the space, with ids 0..MAX_OBJECTS-1
	Each voxel can contain multiple objects

	It is NOT thread-safe.
	
	Hashspace<MAX_OBJECTS, RESOLUTION> hashspace;

	// defines the space over which the hashspace exists
	// expects 'position' values to be given in this world space
	hashspace.reset(world_min, world_max);

	// position an agent
	hashspace.move(id, pos);

	// remove an agent
	hashspace.remove(id);

	// find near agents:
	std::vector<int32_t> nearby;
	int nres = hashspace.query(nearby, NEIGHBOURS_MAX, pos, id, range_of_view);
	for (auto j : nearby) {
		// note: query results will never include self.
		auto& n = objects[j];
		...
	}
*/


// RESOLUTION 5 means 2^5 = 32 voxels in each axis.
template<int MAX_OBJECTS = 1024, int RESOLUTION = 5>
struct Hashspace3D {
	
	struct Object {
		int32_t id;		///< which object ID this is
		int32_t next, prev;
		uint32_t hash;	///< which voxel ID it belongs to (or invalidHash())
		glm::vec3 pos;
	};
	
	struct Voxel {
		/// the linked list of objects in this voxel
		int32_t first;
	};
	
	struct Shell {
		uint32_t start;
		uint32_t end;
	};
	
	Object mObjects[MAX_OBJECTS];
	Voxel mVoxels[(1<<RESOLUTION) * (1<<RESOLUTION) * (1<<RESOLUTION)];
	Shell mShells[(1<<RESOLUTION) * (1<<RESOLUTION)]; // indices into mVoxelsByDistance
	uint32_t mVoxelsByDistance[(1<<RESOLUTION) * (1<<RESOLUTION) * (1<<RESOLUTION)];
	
	uint32_t mShift, mShift2, mDim, mDim2, mDim3, mDimHalf, mWrap, mWrap3;
	
	glm::mat4 world2voxels;
	glm::mat4 voxels2world;
	float world2voxels_scale;
	
	Hashspace3D() {}
	
	Hashspace3D& reset(glm::vec3 world_min, glm::vec3 world_max) {
		
		mShift = RESOLUTION;
		mShift2 = RESOLUTION+RESOLUTION;
		mDim = (1<<RESOLUTION);
		mDim2 = (mDim*mDim);
		mDim3 = (mDim2*mDim);
		mDimHalf = (mDim/2);
		mWrap = (mDim-1);
		mWrap3 = (mDim3-1);
		
		struct ShellData {
			int32_t hash;
			double distance;
			
			ShellData(int32_t hash, double distance) 
			: hash(hash), distance(distance) {}
		};
		
		// create a map of shell radii to voxel hashes:
		std::vector<std::vector<ShellData> > shells;
		shells.resize(mDim2);
		int32_t lim = mDimHalf;
		for (int32_t x= -lim; x < lim; x++) {
			for(int32_t y=-lim; y < lim; y++) {
				for(int32_t z=-lim; z < lim; z++) {
					// each voxel lives at a given distance from the origin:
					double d = x*x+y*y+z*z;
					if (d < mDim2) {
						uint32_t h = hash(x, y, z);
						// add to list: 
						shells[d].push_back(ShellData(h, 0));
					}
				}
			}
		}
		// now pack the shell indices into a sorted list
		// and store in a secondary list the offsets per distance
		int vi = 0;
		for (unsigned d=0; d<mDim2; d++) {
			Shell& shell = mShells[d];
			shell.start = vi; 
			
			std::vector<ShellData>& list = shells[d];
			
			// the order of the elements within a shell is biased
			// earlier 
			//std::reverse(std::begin(list), std::end(list));
			
			std::sort(list.begin(), list.end(), [](ShellData a, ShellData b) { return b.distance > a.distance; });
			
			if (!list.empty()) {
				for (unsigned j=0; j<list.size(); j++) {
					mVoxelsByDistance[vi++] = list[j].hash;
				}
			}
			shell.end = vi;

			// shells run 0..1024 (mdim2)
			// ranges up to 32768 (mdim3)
			//object_post(0, "shell %d: %d..%d", d, shell.start, shell.end);
		}
		
		
		//object_post(0, "mDim %d mDim2 %d mDim3 %d mDimHalf %d %d", mDim, mDim2, mDim3, mDimHalf, lim);
		
	
		// zero out the voxels:
		for (int i=0; i<mDim3; i++) {
			Voxel& o = mVoxels[i];
			o.first = -1;
		}
		
		// zero out the objects:
		for (int i=0; i<MAX_OBJECTS; i++) {
			Object& o = mObjects[i];
			o.id = i;
			o.hash = invalidHash();
			o.next = o.prev = -1;
		}
		
		// define the transforms:
		voxels2world = glm::translate(world_min) * glm::scale((world_max - world_min) / float(mDim));
		// world to normalized:
		world2voxels = glm::inverse(voxels2world);
		
		world2voxels_scale = mDim/((world_max.z - world_min.z));
		
		return *this;
	}
	
	
	int query(std::vector<int32_t>& result, int maxResults, glm::vec3 center, int32_t selfId=-1, float maxRadius=1000.f, float minRadius=0) {
		int nres = 0;
		
		// convert distance in term of voxels:
		minRadius = minRadius * world2voxels_scale;
		maxRadius = maxRadius * world2voxels_scale;
		
		// convert pos:
		auto ctr = world2voxels * glm::vec4(center, 1);
		const uint32_t x = ctr.x+0.5;
		const uint32_t y = ctr.y+0.5;
		const uint32_t z = ctr.z+0.5;
		
		// get shell radii:
		const uint32_t iminr2 = glm::max(uint32_t(0), uint32_t(minRadius*minRadius));
		const uint32_t imaxr2 = glm::min(mDim2, uint32_t(1 + maxRadius*maxRadius));
		
		// move out shell by shell until we have enough results
		for (int s = iminr2; s <= imaxr2 && nres < maxResults; s++) {
			const Shell& shell = mShells[s];
			const uint32_t cellstart = shell.start;
			const uint32_t cellend = shell.end;
			// look at all the voxels in this shell
			// we must check an entire shell, to avoid any spatial bias
			// due to the ordering of voxels within a shell
			for (uint32_t i = cellstart; i < cellend; i++) {
				uint32_t index = hash(x, y, z, mVoxelsByDistance[i]);
				const Voxel& voxel = mVoxels[index];
				// now add any objects in this voxel to the result...
				const int32_t first = voxel.first;
				if (first >= 0) {
					int32_t current = first;
					//int runaway_limit = 100;
					do {
						const Object& o = mObjects[current];
						if (current != o.id) {
							//object_post(0, "corrupt list");
							break;
						}
						if (current != selfId) {
							result.push_back(current);
							nres++;
						}
						current = o.next;
					} while (
							current != first // bail if we looped around the voxel
							//&& nres < maxResults // bail if we have enough hits
							//&& current >= 0  // bail if this isn't a valid object
							//&& --runaway_limit // bail if this has lost control
							); 
				}	
			}
		}
		return nres;
	}
	
	inline void remove(uint32_t objectId) {
		Object& o = mObjects[objectId];
		if (o.hash != invalidHash()) voxel_remove(mVoxels[o.hash], o);
		o.hash = invalidHash();
	}
	
	inline void move(uint32_t objectId, glm::vec3 pos) {
		Object& o = mObjects[objectId];
		o.pos = pos;
		uint32_t newhash = hash(o.pos);
		if (newhash != o.hash) {
			if (o.hash != invalidHash()) voxel_remove(mVoxels[o.hash], o);
			o.hash = newhash;
			voxel_add(mVoxels[newhash], o);
		}
	}

	inline glm::ivec3 dim() { return glm::ivec3(mDim); }
	
	static uint32_t invalidHash() { return UINT_MAX; }
	
	inline uint32_t hash(glm::vec3 v) const { 
		glm::vec4 norm = world2voxels * glm::vec4(v, 1.f);
		return hash(norm[0]+0.5, norm[1]+0.5, norm[2]+0.5); 
	}
	
	// convert x,y,z in range [0..DIM) to unsigned hash:
	// this is also the valid mVoxels index for the corresponding voxel:
	inline uint32_t hash(unsigned x, unsigned y, unsigned z) const {
		return hashx(x)+hashy(y)+hashz(z);
	}
	inline uint32_t hashx(uint32_t v) const { return v & mWrap; }
	inline uint32_t hashy(uint32_t v) const { return (v & mWrap)<<mShift; }
	inline uint32_t hashz(uint32_t v) const { return (v & mWrap)<<mShift2; }
	
	inline uint32_t unhashx(uint32_t h) const { return (h) & mWrap; }
	inline uint32_t unhashy(uint32_t h) const { return (h>>mShift) & mWrap; }
	inline uint32_t unhashz(uint32_t h) const { return (h>>mShift2) & mWrap; }
	
	// generate hash offset by an already generated hash:
	inline uint32_t hash(uint32_t x, uint32_t y, uint32_t z, uint32_t offset) const {
		return	hashx(unhashx(offset) + x) +
		hashy(unhashy(offset) + y) +
		hashz(unhashz(offset) + z);
	}
	
	// this is definitely not thread-safe.
	inline Hashspace3D& voxel_add(Voxel& v, Object& o) {
		if (v.first >= 0) {
			Object& first = mObjects[v.first];
			Object& last = mObjects[first.prev];
			// add to tail:
			o.prev = last.id;
			o.next = first.id;
			last.next = first.prev = o.id;
		} else {
			// unique:
			v.first = o.prev = o.next = o.id;
		}
		return *this;
	}
	
	// this is definitely not thread-safe.
	inline Hashspace3D& voxel_remove(Voxel& v, Object& o) {
		if (o.id == o.prev) {	// voxel only has 1 item
			v.first = -1;
		} else {
			Object& prev = mObjects[o.prev];
			Object& next = mObjects[o.next];
			prev.next = next.id;
			next.prev = prev.id;
			// update head pointer?
			if (v.first == o.id) { v.first = next.id; }
		}
		// leave the object clean:
		o.prev = o.next = -1;
		return *this;
	}

};

// RESOLUTION 5 means 2^5 = 32 voxels in each axis.
template<int MAX_OBJECTS = 1024, int RESOLUTION = 5>
struct Hashspace2D {
	
	struct Object {
		int32_t id;		///< which object ID this is
		int32_t next, prev;
		uint32_t hash;	///< which voxel ID it belongs to (or invalidHash())
		glm::vec2 pos;
	};
	
	struct Voxel {
		/// the linked list of objects in this voxel
		int32_t first;
	};
	
	struct Shell {
		uint32_t start;
		uint32_t end;
	};
	
	Object mObjects[MAX_OBJECTS];
	Voxel mVoxels[(1<<RESOLUTION) * (1<<RESOLUTION) * (1<<RESOLUTION)];
	Shell mShells[(1<<RESOLUTION) * (1<<RESOLUTION)]; // indices into mVoxelsByDistance
	uint32_t mVoxelsByDistance[(1<<RESOLUTION) * (1<<RESOLUTION) * (1<<RESOLUTION)];
	
	uint32_t mShift, mShift2, mDim, mDim2, mDimHalf, mWrap, mWrap2;
	
	glm::mat3 world2voxels;
	glm::mat3 voxels2world;
	float world2voxels_scale;
	
	Hashspace2D& reset(glm::vec2 world_min, glm::vec2 world_max) {
		
		mShift = RESOLUTION;
		mShift2 = RESOLUTION+RESOLUTION;
		mDim = (1<<RESOLUTION);
		mDim2 = (mDim*mDim);
		mDimHalf = (mDim/2);
		mWrap = (mDim-1);
		mWrap2 = (mDim2-1);
		
		struct ShellData {
			int32_t hash;
			double distance;
			
			ShellData(int32_t hash, double distance) 
			: hash(hash), distance(distance) {}
		};
		
		// create a map of shell radii to voxel hashes:
		std::vector<std::vector<ShellData> > shells;
		shells.resize(mDim2); 
		int32_t lim = mDimHalf;
		for (int32_t x= -lim; x < lim; x++) {
			for(int32_t y=-lim; y < lim; y++) {
				// each voxel lives at a given distance from the origin:
				double d = x*x + y*y;
				if (d < mDim2) {
					uint32_t h = hash(x, y);
					// add to list: 
					shells[d].push_back(ShellData(h, 0));
				}
			}
		}

		// now pack the shell indices into a sorted list
		// and store in a secondary list the offsets per distance
		int vi = 0;
		for (unsigned d=0; d<mDim2; d++) {
			Shell& shell = mShells[d];
			shell.start = vi; 
			
			std::vector<ShellData>& list = shells[d];
			
			// the order of the elements within a shell is biased
			// earlier 
			//std::reverse(std::begin(list), std::end(list));
			
			//std::sort(list.begin(), list.end(), [](ShellData a, ShellData b) { return b.distance > a.distance; });
			
			if (!list.empty()) {
				for (unsigned j=0; j<list.size(); j++) {
					mVoxelsByDistance[vi++] = list[j].hash;
				}
			}
			shell.end = vi;

			// shells run 0..1024 (mdim2)
			// ranges up to 32768 (mdim3)
			//object_post(0, "shell %d: %d..%d", d, shell.start, shell.end);
		}
		
		
		//object_post(0, "mDim %d mDim2 %d mDim3 %d mDimHalf %d %d", mDim, mDim2, mDim3, mDimHalf, lim);
		
	
		// zero out the voxels:
		for (int i=0; i<mDim2; i++) {
			Voxel& o = mVoxels[i];
			o.first = -1;
		}
		
		// zero out the objects:
		for (int i=0; i<MAX_OBJECTS; i++) {
			Object& o = mObjects[i];
			o.id = i;
			o.hash = invalidHash();
			o.next = o.prev = -1;
		}
		
		// define the transforms:
		voxels2world = glm::translate(glm::mat3(), world_min) 
					 * (glm::scale(glm::mat3(), world_max - world_min) / float(mDim));
		
		//glm::translate(glm::mat3(), world_min) * glm::scale(glm::mat3(), world_max - world_min);
		// world to normalized:
		world2voxels = glm::inverse(voxels2world);
		
		world2voxels_scale = mDim/((world_max.x - world_min.x));
		
		return *this;
	}
	
	
	int query(std::vector<int32_t>& result, int maxResults, glm::vec2 center, int32_t selfId=-1, float maxRadius=1000.f, float minRadius=0) {
		int nres = 0;
		
		// convert distance in term of voxels:
		minRadius = minRadius * world2voxels_scale;
		maxRadius = maxRadius * world2voxels_scale;
		
		// convert pos:
		auto ctr = transform(world2voxels, center); //world2voxels * center;
		const uint32_t x = ctr.x+0.5;
		const uint32_t y = ctr.y+0.5;
	
		// get shell radii:
		const uint32_t iminr2 = glm::max(uint32_t(0), uint32_t(minRadius*minRadius));
		const uint32_t imaxr2 = glm::min(mDim2, uint32_t(1 + maxRadius*maxRadius));
		
		// move out shell by shell until we have enough results
		for (int s = iminr2; s <= imaxr2 && nres < maxResults; s++) {
			const Shell& shell = mShells[s];
			const uint32_t cellstart = shell.start;
			const uint32_t cellend = shell.end;
			// look at all the voxels in this shell
			// we must check an entire shell, to avoid any spatial bias
			// due to the ordering of voxels within a shell
			for (uint32_t i = cellstart; i < cellend; i++) {
				uint32_t index = hash(x, y, mVoxelsByDistance[i]);
				const Voxel& voxel = mVoxels[index];
				// now add any objects in this voxel to the result...
				const int32_t first = voxel.first;
				if (first >= 0) {
					int32_t current = first;
					//int runaway_limit = 100;
					do {
						const Object& o = mObjects[current];
						if (current != o.id) {
							//object_post(0, "corrupt list");
							break;
						}
						if (current != selfId) {
							result.push_back(current);
							nres++;
						}
						current = o.next;
					} while (
							current != first // bail if we looped around the voxel
							//&& nres < maxResults // bail if we have enough hits
							//&& current >= 0  // bail if this isn't a valid object
							//&& --runaway_limit // bail if this has lost control
							); 
				}	
			}
		}
		return nres;
	}
	
	inline void remove(uint32_t objectId) {
		Object& o = mObjects[objectId];
		if (o.hash != invalidHash()) voxel_remove(mVoxels[o.hash], o);
		o.hash = invalidHash();
	}
	
	inline void move(uint32_t objectId, glm::vec2 pos) {
		Object& o = mObjects[objectId];
		o.pos = pos;
		uint32_t newhash = hash(o.pos);
		if (newhash != o.hash) {
			if (o.hash != invalidHash()) voxel_remove(mVoxels[o.hash], o);
			o.hash = newhash;
			voxel_add(mVoxels[newhash], o);
		}
	}

	inline glm::ivec2 dim() { return glm::ivec2(mDim); }
	
	static uint32_t invalidHash() { return UINT_MAX; }
	
	inline uint32_t hash(glm::vec2 v) const { 
		glm::vec2 norm = transform(world2voxels, v);//world2voxels * v; 
		return hash(norm[0]+0.5, norm[1]+0.5); 
	}
	
	// convert x,y,z in range [0..DIM) to unsigned hash:
	// this is also the valid mVoxels index for the corresponding voxel:
	inline uint32_t hash(unsigned x, unsigned y) const {
		return hashx(x)+hashy(y);
	}
	inline uint32_t hashx(uint32_t v) const { return v & mWrap; }
	inline uint32_t hashy(uint32_t v) const { return (v & mWrap)<<mShift; }
	
	inline uint32_t unhashx(uint32_t h) const { return (h) & mWrap; }
	inline uint32_t unhashy(uint32_t h) const { return (h>>mShift) & mWrap; }
	
	// generate hash offset by an already generated hash:
	inline uint32_t hash(uint32_t x, uint32_t y, uint32_t offset) const {
		return	hashx(unhashx(offset) + x) +
		hashy(unhashy(offset) + y);
	}
	
	// this is definitely not thread-safe.
	inline Hashspace2D& voxel_add(Voxel& v, Object& o) {
		if (v.first >= 0) {
			Object& first = mObjects[v.first];
			Object& last = mObjects[first.prev];
			// add to tail:
			o.prev = last.id;
			o.next = first.id;
			last.next = first.prev = o.id;
		} else {
			// unique:
			v.first = o.prev = o.next = o.id;
		}
		return *this;
	}
	
	// this is definitely not thread-safe.
	inline Hashspace2D& voxel_remove(Voxel& v, Object& o) {
		if (o.id == o.prev) {	// voxel only has 1 item
			v.first = -1;
		} else {
			Object& prev = mObjects[o.prev];
			Object& next = mObjects[o.next];
			prev.next = next.id;
			next.prev = prev.id;
			// update head pointer?
			if (v.first == o.id) { v.first = next.id; }
		}
		// leave the object clean:
		o.prev = o.next = -1;
		return *this;
	}

};


/*
	Would be nice to support different resolutions in each axis, to better fit data
	
	There are two scale implications: 	
		1. relative axis scales of the voxels (aspect ratios)
		2. relative axis scales of the world space (volume shape)

	When performing a neighbour query, we use pre-based spheroid shells, 
		but really these should these be spheres in world-space
	I.e. a query() radius is a world-space radius.
	Which means the baked shells need to take into account the warping due to non-uniform volume & voxel sizes
	i.e. the calculation of a voxel distance from origin depends on these non-uniform factors.

	
*/	
// RESOLUTION 5 means 2^5 = 32 voxels in each axis.
template<int MAX_OBJECTS=1024, int RESX=5, int RESY=5, int RESZ=5>
struct Hashspace3D3 {

	struct Voxel {
		/// the linked list of objects in this voxel
		int32_t first;
	};

	struct Shell {
		uint32_t start;
		uint32_t end;
	};

	struct Object {
		int32_t id;		///< which object ID this is
		int32_t next, prev;
		uint32_t hash;	///< which voxel ID it belongs to (or invalidHash())
		glm::vec3 pos;
	};

	// the actual voxel grid:
	Voxel mVoxels[(1<<RESX) * (1<<RESY) * (1<<RESZ)];
	// mVoxels indices into mVoxelsByDistance
	// what is the longest distance?
	Shell mShells[glm::max(glm::max(RESX, RESY), RESZ)];
	// the book-keeping of unique objects in the space
	Object mObjects[MAX_OBJECTS];
};

#endif // AL_HASHSPACE_H
