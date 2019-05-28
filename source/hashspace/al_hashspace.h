#ifndef AL_HASHSPACE_H
#define AL_HASHSPACE_H

#include <vector>
#include <algorithm>

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
	
	// a shell is a container of all voxels of approximately the same distance from the origin
	// (distances are approximated as integer squared-distance)
	// the start & end values are indices into the mVoxelsByDistance array.
	// the largest complete shell radius possible is mDimHalf;
	// as any radius larger than this would be an incomplete shell
	struct Shell {
		uint32_t start;
		uint32_t end;
	};
	
	Object mObjects[MAX_OBJECTS];
	Voxel mVoxels[(1<<RESOLUTION) * (1<<RESOLUTION) * (1<<RESOLUTION)];
	Shell mShells[((1<<RESOLUTION)/2) * ((1<<RESOLUTION)/2)]; // indices into mVoxelsByDistance
	uint32_t mVoxelsByDistance[(1<<RESOLUTION) * (1<<RESOLUTION) * (1<<RESOLUTION)];
	
	uint32_t mShift, mShift2, mDim, mDim2, mDim3, mDimHalf, mWrap, mWrap3;
	uint32_t mMaxRad2;
	
	glm::mat4 world2voxels;
	glm::mat4 voxels2world;
	float world2voxels_scale;
	
	Hashspace3D& reset(glm::vec3 world_min, glm::vec3 world_max) {
		
		mShift = RESOLUTION;
		mShift2 = RESOLUTION+RESOLUTION;
		mDim = (1<<RESOLUTION);
		mDim2 = (mDim*mDim);
		mDim3 = (mDim2*mDim);
		mWrap = (mDim-1);
		mWrap3 = (mDim3-1);
		mDimHalf = (mDim/2);
		// the largest valid query of distance-squared
		mMaxRad2 = mDimHalf * mDimHalf; // 256 for RESOLUTION=5
		
		// zero out the voxels:
		for (int i=0; i<mDim3; i++) {
			Voxel& o = mVoxels[i];
			o.first = invalidObject();
		}
		
		// zero out the objects:
		for (int i=0; i<MAX_OBJECTS; i++) {
			Object& o = mObjects[i];
			o.id = i;
			o.hash = invalidHash();
			o.next = o.prev = invalidObject();
		}
		
		// define the transforms:
		world2voxels_scale = mDim/((world_max.z - world_min.z));
		voxels2world = glm::translate(world_min) * glm::scale((world_max - world_min) / float(mDim));
		// world to normalized:
		world2voxels = glm::inverse(voxels2world);
		
		// estimate no. of voxels within max shell sphere (volume of max radius sphere)
		//uint32_t validvoxels = (4.f/3.f)*M_PI*(mDimHalf * mDimHalf * mDimHalf);
		//post("estimate valid voxels %d", validvoxels); // tends to over-estimate a little
		
		struct ShellData {
			int32_t hash;
			double distance_squared;
			
			ShellData(int32_t hash, double distance_squared)
			: hash(hash), distance_squared(distance_squared) {}
		};
		
		
		// create a map of shell radii to voxel hashes:
		std::vector<std::vector<ShellData> > shells;
		shells.resize(mMaxRad2);
		int32_t lim = mDimHalf;
		
		// for each voxel in the cube of -mDimHalf to mDimHalf,
		for (int32_t x= -lim; x < lim; x++) {
			for(int32_t y=-lim; y < lim; y++) {
				for(int32_t z=-lim; z < lim; z++) {
					// compute distance from the origin:
					double d = (x*x+y*y+z*z);
					int32_t di = (int)d;
					// if within the mMaxRad2 sphere
					if (di < mMaxRad2) {
						// add to list of valid shells
						shells[di].push_back(ShellData(hash(x, y, z), d));
					}
				}
			}
		}
		
		// first sort the ShellData members by distance-squared:
		for (unsigned d=0; d<shells.size(); d++) {
			std::sort(shells[d].begin(), shells[d].end(), [](ShellData a, ShellData b) { return b.distance_squared > a.distance_squared; });
		}
		
		// now pack the shell indices into a sorted list
		// and store in a secondary list the offsets per distance
		int vi = 0; // index into mVoxelsByDistance
		// for each shell:
		for (unsigned d=0; d<shells.size(); d++) {
			std::vector<ShellData>& list = shells[d];
			
			// set the shell start index:
			Shell& shell = mShells[d];
			shell.start = vi;
			//post("shell %d:", d);
			
			if (!list.empty()) {
				for (unsigned j=0; j<list.size(); j++) {
					uint32_t h = list[j].hash;
					mVoxelsByDistance[vi++] = h;
				}
			}
			
			// set the shell end index:
			shell.end = vi;
			
			// shells run 0..1024 (mDimHalf)
			// ranges up to 32768 (mdim3)
			// it is possible that some shells have no entries.
			//post("shell %d contains voxels %d..%d", d, shell.start, shell.end-1);
		}
		
		//object_post(0, "mDim %d mDim2 %d mDim3 %d mDimHalf %d max radius squared: %d", mDim, mDim2, mDim3, mDimHalf, mMaxRad2);
		return *this;
	}
	
	// returns invalidHash() if not found:
	int32_t first(glm::vec3 center, int32_t selfId=invalidObject(), float maxRadius=1000.f, float minRadius=0, bool toroidal=true) {
		
		// convert distance in term of voxels:
		minRadius = minRadius * world2voxels_scale;
		maxRadius = maxRadius * world2voxels_scale;
		
		// get shell radii:
		const uint32_t iminr2 = glm::max(uint32_t(0), uint32_t(minRadius*minRadius));
		const uint32_t imaxr2 = glm::min(mMaxRad2, 1 + uint32_t(ceil(maxRadius*maxRadius)));

		// convert pos:
		auto ctr = world2voxels * glm::vec4(center, 1);
		const uint32_t x = ctr.x;
		const uint32_t y = ctr.y;
		const uint32_t z = ctr.z;
		
		//post("query point %s for voxel point %f => %d %d %d", glm::to_string(center), glm::to_string(ctr), x, y, z);

		//post("query shells %d to %d for point %d %d %d", iminr2, imaxr2, x, y, z);
		
		
		// move out shell by shell until we have enough results
		for (int s = iminr2; s < imaxr2; s++) {

			//post("shell %d", s);
			
			const Shell& shell = mShells[s];
			const uint32_t cellstart = shell.start;
			const uint32_t cellend = shell.end;
			
			//post("search shell %d, cells %d to %d", s, cellstart, cellend);
			
			// look at all the voxels in this shell
			// we must check an entire shell, to avoid any spatial bias
			// due to the ordering of voxels within a shell
			for (uint32_t i = cellstart; i < cellend; i++) {
				//post("cell");
				
				// use the current cell's hash to offset our query
				// (i.e., translate to the voxel's center)
				uint32_t offset = mVoxelsByDistance[i];
				uint32_t index = toroidal
				? hash(x, y, z, offset)
				: hash_nontoroidal(x, y, z, offset);
				
				if (!isValid(index)) continue;
				
				const Voxel& voxel = mVoxels[index];
				// now add any objects in this voxel to the result...
				const int32_t first = voxel.first;
				int32_t prev = invalidObject();
				if (first >= 0) {
					
					int32_t current = first;
					//post(".    check cell member %d after %d", current, prev);
					int runaway_limit = MAX_OBJECTS;
					do {
						const Object& o = mObjects[current];
						if (current != o.id) {
							fprintf(stderr, "hashspace list is corrupt - reset\n");
							break;
						}
						if (current != selfId) {
							return current;
						}
						prev = current;
						current = o.next;
					} while (
							 current != first // bail if we looped around the voxel
							 && current != prev
							 && current >= 0  // bail if this isn't a valid object
							 && --runaway_limit // bail if this has lost control
							 );
				}
			}
		}
		
		// TODO: would be nice to set the nearest as the first item...
		
		return invalidObject();
	}
	
	int query(std::vector<int32_t>& result, int maxResults, glm::vec3 center, int32_t selfId=invalidObject(), float maxRadius=1000.f, float minRadius=0, bool toroidal=true) {
		int nres = 0;

		//post("query");
		
		// convert distance in term of voxels:
		minRadius = minRadius * world2voxels_scale;
		maxRadius = maxRadius * world2voxels_scale;
		
		// get shell radii:
		const uint32_t iminr2 = glm::max(uint32_t(0), uint32_t(minRadius*minRadius));
		const uint32_t imaxr2 = glm::min(mMaxRad2, 1 + uint32_t(ceil(maxRadius*maxRadius)));

		
		// convert pos:
		auto ctr = world2voxels * glm::vec4(center, 1);
		const uint32_t x = ctr.x;
		const uint32_t y = ctr.y;
		const uint32_t z = ctr.z;
		
		//post("query point %s for voxel point %f => %d %d %d", glm::to_string(center), glm::to_string(ctr), x, y, z);

		//post("query shells %d to %d for point %d %d %d", iminr2, imaxr2, x, y, z);
		
		
		// move out shell by shell until we have enough results
		for (int s = iminr2; s < imaxr2 && nres < maxResults; s++) {

			//post("shell %d", s);
			
			const Shell& shell = mShells[s];
			const uint32_t cellstart = shell.start;
			const uint32_t cellend = shell.end;
			
			//post("search shell %d, cells %d to %d", s, cellstart, cellend);
			
			// look at all the voxels in this shell
			// we must check an entire shell, to avoid any spatial bias
			// due to the ordering of voxels within a shell
			for (uint32_t i = cellstart; i < cellend; i++) {
				//post("cell");
				
				// use the current cell's hash to offset our query
				// (i.e., translate to the voxel's center)
				uint32_t offset = mVoxelsByDistance[i];
				uint32_t index = toroidal
				? hash(x, y, z, offset)
				: hash_nontoroidal(x, y, z, offset);
				
				//uint32_t ux = unhashx(offset);
				//uint32_t uy = unhashy(offset);
				//uint32_t uz = unhashz(offset);

				//int32_t ox = ((ux+mDimHalf) & mWrap) - mDimHalf;
				//int32_t oy = ((uy+mDimHalf) & mWrap) - mDimHalf;
				//int32_t oz = ((uz+mDimHalf) & mWrap) - mDimHalf;

				//uint32_t hx = hashx_nontoroidal(ox + x);
				//uint32_t hy = hashy_nontoroidal(oy + y);
				//uint32_t hz = hashz_nontoroidal(oz + z);
				//post(".  search cell %d, offset %d (%d %d %d) = index %d", i, offset, ux, uy, uz, index);
				//post(".  => real offset %d %d %d -> pos %d %d %d => hashed %d %d %d", ox, oy, oz, x + ox, y + oy, z + oz, hx, hy, hz);
				
				if (!isValid(index)) continue;
				
				const Voxel& voxel = mVoxels[index];
				// now add any objects in this voxel to the result...
				const int32_t first = voxel.first;
				int32_t prev = invalidObject();
				if (first >= 0) {
					
					
					int32_t current = first;
					//post(".    check cell member %d after %d", current, prev);
					int runaway_limit = MAX_OBJECTS;
					do {
						const Object& o = mObjects[current];
						if (current != o.id) {
							fprintf(stderr, "hashspace list is corrupt - reset\n");
							break;
						}
						if (current != selfId) {
							result.push_back(current);
							nres++;
						}
						prev = current;
						current = o.next;
					} while (
							 current != first // bail if we looped around the voxel
							 && current != prev
							 && current >= 0  // bail if this isn't a valid object
							 && --runaway_limit // bail if this has lost control
							 );
				}
			}
		}
		
		// TODO: would be nice to set the nearest as the first item...
		
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
		//post("moving %d to %f %f %f %d", objectId, o.pos.x, o.pos.y, o.pos.z, newhash);
		if (newhash != o.hash) {
			if (o.hash != invalidHash()) voxel_remove(mVoxels[o.hash], o);
			o.hash = newhash;
			if (newhash != invalidHash()) voxel_add(mVoxels[newhash], o);
		}
	}
	
	inline glm::ivec3 dim() { return glm::ivec3(mDim); }


	static uint32_t invalidObject() { return -1; }
	
	static uint32_t invalidHash() { return UINT_MAX; }
	static bool isValid(uint32_t h) { return h != invalidHash(); }
	
	inline uint32_t hash(glm::vec3 v) const {
		glm::vec4 norm = world2voxels * glm::vec4(v, 1.f);
		//return hash(norm[0]+0.5, norm[1]+0.5, norm[2]+0.5);
		return hash(norm[0], norm[1], norm[2]);
	}
	
	// convert x,y,z in range [0..DIM) to unsigned hash:
	// this is also the valid mVoxels index for the corresponding voxel:
	inline uint32_t hash(unsigned x, unsigned y, unsigned z) const {
		return hashx(x)+hashy(y)+hashz(z);
	}
	inline uint32_t hashx(uint32_t v) const { return v & mWrap; }
	inline uint32_t hashy(uint32_t v) const { return (v & mWrap)<<mShift; }
	inline uint32_t hashz(uint32_t v) const { return (v & mWrap)<<mShift2; }
	
	
	inline uint32_t hashx_nontoroidal(int32_t v) const {
		return (v >= 0 && v <= mWrap) ? v 		    : invalidHash();
	}
	inline uint32_t hashy_nontoroidal(int32_t v) const {
		return (v >= 0 && v <= mWrap) ? v<<mShift  : invalidHash();
	}
	inline uint32_t hashz_nontoroidal(int32_t v) const {
		return (v >= 0 && v <= mWrap) ? v<<mShift2 : invalidHash();
	}
	
	inline uint32_t unhashx(uint32_t h) const { return (h) & mWrap; }
	inline uint32_t unhashy(uint32_t h) const { return (h>>mShift) & mWrap; }
	inline uint32_t unhashz(uint32_t h) const { return (h>>mShift2) & mWrap; }
	
	// generate hash offset by an already generated hash:
	inline uint32_t hash(uint32_t x, uint32_t y, uint32_t z, uint32_t offset) const {
		return	hashx(unhashx(offset) + x) +
				hashy(unhashy(offset) + y) +
				hashz(unhashz(offset) + z);
	}
	
	inline uint32_t hash_nontoroidal(uint32_t x, uint32_t y, uint32_t z, uint32_t offset) const {
		// get signed version of offsets:
		int32_t ox = (((unhashx(offset)+mDimHalf) & mWrap) - mDimHalf);
		int32_t oy = (((unhashy(offset)+mDimHalf) & mWrap) - mDimHalf);
		int32_t oz = (((unhashz(offset)+mDimHalf) & mWrap) - mDimHalf);
		// apply to position & hash
		x = hashx_nontoroidal(ox + x);
		y = hashy_nontoroidal(oy + y);
		z = hashz_nontoroidal(oz + z);
		// validate & combine:
		return isValid(x) && isValid(y) && isValid(z)
		? x + y + z
		: invalidHash();

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
		o.prev = o.next = invalidObject();
		return *this;
	}
	
};

/*
	Would be nice to support different resolutions in each axis, to better fit data in a non-cube world

	It is preferable for voxels to still have 1:1:1 aspect ratios however,
	so this only fully works if the world dim is itself in pow2 ratios. 
	That is, there is still a scalar `world2voxels_scale`
	
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
		glm::vec3 pos; // real-world 
	};

	// the book-keeping of unique objects in the space
	Object mObjects[MAX_OBJECTS];
	// the actual voxel grid:
	Voxel mVoxels[(1<<RESX) * (1<<RESY) * (1<<RESZ)];
	// mVoxels indices into mVoxelsByDistance
	// what is the longest distance?
	Shell mShells[((1<<std::max(std::max(RESX, RESY), RESZ))/2) * ((1<<std::max(std::max(RESX, RESY), RESZ))/2)];
	uint32_t mVoxelsByDistance[(1<<std::max(std::max(RESX, RESY), RESZ)) * (1<<std::max(std::max(RESX, RESY), RESZ)) * (1<<std::max(std::max(RESX, RESY), RESZ))];

	uint32_t mShift, mShift2, mShift3;
	uint32_t mShiftX2, mShiftY2, mShiftZ2;
	uint32_t mDimX, mDimY, mDimZ;
	uint32_t mDim3;
	uint32_t mWrapX, mWrapY, mWrapZ;
	uint32_t mWrap3;
	uint32_t mDimHalfX, mDimHalfY, mDimHalfZ;
	uint32_t mMaxRad2;

	glm::mat4 world2voxels;
	glm::mat4 voxels2world;
	float world2voxels_scale;

	Hashspace3D3& reset(glm::vec3 world_min, glm::vec3 world_max) {
		mShift = RESX;
		mShift2 = RESX+RESY;
		mShift3 = RESX+RESY+RESZ;
		mDimX = (1<<RESX);
		mDimY = (1<<RESY);
		mDimZ = (1<<RESZ);
		mDim3 = mDimX*mDimY*mDimZ;
		mWrapX = (mDimX-1);
		mWrapY = (mDimY-1);
		mWrapZ = (mDimZ-1);
		mWrap3 = (mDim3-1);
		mDimHalfX = (mDimX/2);
		mDimHalfY = (mDimY/2);
		mDimHalfZ = (mDimZ/2);

		// the largest valid query of distance-squared
		mMaxRad2 = std::max(std::max(mDimX, mDimY), mDimZ)/2 * std::max(std::max(mDimX, mDimY), mDimZ)/2;

		// zero out the voxels:
		for (int i=0; i<mDim3; i++) {
			Voxel& o = mVoxels[i];
			o.first = invalidObject();
		}
		
		// zero out the objects:
		for (int i=0; i<MAX_OBJECTS; i++) {
			Object& o = mObjects[i];
			o.id = i;
			o.hash = invalidHash();
			o.next = o.prev = invalidObject();
		}
		
		// define the transforms:
		world2voxels_scale = mDimX/((world_max.x - world_min.x));
		voxels2world = glm::translate(world_min) * glm::scale((world_max - world_min) / glm::vec3(mDimX, mDimY, mDimZ));
		// world to normalized:
		world2voxels = glm::inverse(voxels2world);

		// estimate no. of voxels within max shell sphere (volume of max radius sphere)
		//uint32_t validvoxels = (4.f/3.f)*M_PI*(mDimHalf * mDimHalf * mDimHalf);
		//post("estimate valid voxels %d", validvoxels); // tends to over-estimate a little
		
		struct ShellData {
			int32_t hash;
			double distance_squared;
			
			ShellData(int32_t hash, double distance_squared)
			: hash(hash), distance_squared(distance_squared) {}
		};

		// create a map of shell radii to voxel hashes:
		std::vector<std::vector<ShellData> > shells;
		shells.resize(mMaxRad2);
		int32_t limX = mDimHalfX;
		int32_t limY = mDimHalfY;
		int32_t limZ = mDimHalfZ;
		
		// for each voxel in the cube of -mDimHalf to mDimHalf,
		for (int32_t x= -limX; x < limX; x++) {
			for(int32_t y=-limY; y < limY; y++) {
				for(int32_t z=-limZ; z < limZ; z++) {
					// compute distance from the origin:
					double d = (x*x+y*y+z*z);
					int32_t di = (int)d;
					// if within the mMaxRad2 sphere
					if (di < mMaxRad2) {
						// add to list of valid shells
						shells[di].push_back(ShellData(hash(x, y, z), d));
					}
				}
			}
		}

		// first sort the ShellData members by distance-squared:
		for (unsigned d=0; d<shells.size(); d++) {
			std::sort(shells[d].begin(), shells[d].end(), [](ShellData a, ShellData b) { return b.distance_squared > a.distance_squared; });
		}

		// now pack the shell indices into a sorted list
		// and store in a secondary list the offsets per distance
		int vi = 0; // index into mVoxelsByDistance
		// for each shell:
		for (unsigned d=0; d<shells.size(); d++) {
			std::vector<ShellData>& list = shells[d];
			
			// set the shell start index:
			Shell& shell = mShells[d];
			shell.start = vi;
			//post("shell %d:", d);
			
			if (!list.empty()) {
				for (unsigned j=0; j<list.size(); j++) {
					uint32_t h = list[j].hash;
					mVoxelsByDistance[vi++] = h;
				}
			}
			
			// set the shell end index:
			shell.end = vi;
			
			// shells run 0..1024 (mDimHalf)
			// ranges up to 32768 (mdim3)
			// it is possible that some shells have no entries.
			//post("shell %d contains voxels %d..%d", d, shell.start, shell.end-1);
		}
		
		//object_post(0, "mDim %d mDim2 %d mDim3 %d mDimHalf %d max radius squared: %d", mDim, mDim2, mDim3, mDimHalf, mMaxRad2);
		return *this;
	}
	

	
	// returns invalidHash() if not found:
	int32_t first(glm::vec3 center, int32_t selfId=invalidObject(), float maxRadius=1000.f, float minRadius=0, bool toroidal=true) {
		
		// convert distance in term of voxels:
		minRadius = minRadius * world2voxels_scale;
		maxRadius = maxRadius * world2voxels_scale;
		
		// get shell radii:
		const uint32_t iminr2 = glm::max(uint32_t(0), uint32_t(minRadius*minRadius));
		const uint32_t imaxr2 = glm::min(mMaxRad2, 1 + uint32_t(ceil(maxRadius*maxRadius)));

		// convert pos:
		auto ctr = world2voxels * glm::vec4(center, 1);
		const uint32_t x = ctr.x;
		const uint32_t y = ctr.y;
		const uint32_t z = ctr.z;
		
		//post("query point %s for voxel point %f => %d %d %d", glm::to_string(center), glm::to_string(ctr), x, y, z);

		//post("query shells %d to %d for point %d %d %d", iminr2, imaxr2, x, y, z);
		
		
		// move out shell by shell until we have enough results
		for (int s = iminr2; s < imaxr2; s++) {

			//post("shell %d", s);
			
			const Shell& shell = mShells[s];
			const uint32_t cellstart = shell.start;
			const uint32_t cellend = shell.end;
			
			//post("search shell %d, cells %d to %d", s, cellstart, cellend);
			
			// look at all the voxels in this shell
			// we must check an entire shell, to avoid any spatial bias
			// due to the ordering of voxels within a shell
			for (uint32_t i = cellstart; i < cellend; i++) {
				//post("cell");
				
				// use the current cell's hash to offset our query
				// (i.e., translate to the voxel's center)
				uint32_t offset = mVoxelsByDistance[i];
				uint32_t index = toroidal
				? hash(x, y, z, offset)
				: hash_nontoroidal(x, y, z, offset);
				
				if (!isValid(index)) continue;
				
				const Voxel& voxel = mVoxels[index];
				// now add any objects in this voxel to the result...
				const int32_t first = voxel.first;
				int32_t prev = invalidObject();
				if (first >= 0) {
					
					int32_t current = first;
					//post(".    check cell member %d after %d", current, prev);
					int runaway_limit = MAX_OBJECTS;
					do {
						const Object& o = mObjects[current];
						if (current != o.id) {
							fprintf(stderr, "hashspace list is corrupt - reset\n");
							break;
						}
						if (current != selfId) {
							return current;
						}
						prev = current;
						current = o.next;
					} while (
							 current != first // bail if we looped around the voxel
							 && current != prev
							 && current >= 0  // bail if this isn't a valid object
							 && --runaway_limit // bail if this has lost control
							 );
				}
			}
		}
		
		// TODO: would be nice to set the nearest as the first item...
		
		return invalidObject();
	}
	
	int query(std::vector<int32_t>& result, int maxResults, glm::vec3 center, int32_t selfId=-1, float maxRadius=1000.f, float minRadius=0, bool toroidal=true) {
		int nres = 0;

		//post("query");
		
		// convert distance in term of voxels:
		minRadius = minRadius * world2voxels_scale;
		maxRadius = maxRadius * world2voxels_scale;
		
		// get shell radii:
		const uint32_t iminr2 = glm::max(uint32_t(0), uint32_t(minRadius*minRadius));
		const uint32_t imaxr2 = glm::min(mMaxRad2, 1 + uint32_t(ceil(maxRadius*maxRadius)));

		
		// convert pos:
		auto ctr = world2voxels * glm::vec4(center, 1);
		const uint32_t x = ctr.x;
		const uint32_t y = ctr.y;
		const uint32_t z = ctr.z;
		
		//post("query point %s for voxel point %f => %d %d %d", glm::to_string(center), glm::to_string(ctr), x, y, z);

		//post("query shells %d to %d for point %d %d %d", iminr2, imaxr2, x, y, z);
		
		
		// move out shell by shell until we have enough results
		for (int s = iminr2; s < imaxr2 && nres < maxResults; s++) {

			//post("shell %d", s);
			
			const Shell& shell = mShells[s];
			const uint32_t cellstart = shell.start;
			const uint32_t cellend = shell.end;
			
			//post("search shell %d, cells %d to %d", s, cellstart, cellend);
			
			// look at all the voxels in this shell
			// we must check an entire shell, to avoid any spatial bias
			// due to the ordering of voxels within a shell
			for (uint32_t i = cellstart; i < cellend; i++) {
				//post("cell");
				
				// use the current cell's hash to offset our query
				// (i.e., translate to the voxel's center)
				uint32_t offset = mVoxelsByDistance[i];
				uint32_t index = toroidal
				? hash(x, y, z, offset)
				: hash_nontoroidal(x, y, z, offset);
				
				//uint32_t ux = unhashx(offset);
				//uint32_t uy = unhashy(offset);
				//uint32_t uz = unhashz(offset);

				//int32_t ox = ((ux+mDimHalf) & mWrap) - mDimHalf;
				//int32_t oy = ((uy+mDimHalf) & mWrap) - mDimHalf;
				//int32_t oz = ((uz+mDimHalf) & mWrap) - mDimHalf;

				//uint32_t hx = hashx_nontoroidal(ox + x);
				//uint32_t hy = hashy_nontoroidal(oy + y);
				//uint32_t hz = hashz_nontoroidal(oz + z);
				//post(".  search cell %d, offset %d (%d %d %d) = index %d", i, offset, ux, uy, uz, index);
				//post(".  => real offset %d %d %d -> pos %d %d %d => hashed %d %d %d", ox, oy, oz, x + ox, y + oy, z + oz, hx, hy, hz);
				
				if (!isValid(index)) continue;
				
				const Voxel& voxel = mVoxels[index];
				// now add any objects in this voxel to the result...
				const int32_t first = voxel.first;
				int32_t prev = invalidObject();
				if (first >= 0) {
					
					
					int32_t current = first;
					//post(".    check cell member %d after %d", current, prev);
					int runaway_limit = MAX_OBJECTS;
					do {
						const Object& o = mObjects[current];
						if (current != o.id) {
							fprintf(stderr, "hashspace list is corrupt - reset\n");
							break;
						}
						if (current != selfId) {
							result.push_back(current);
							nres++;
						}
						prev = current;
						current = o.next;
					} while (
							 current != first // bail if we looped around the voxel
							 && current != prev
							 && current >= 0  // bail if this isn't a valid object
							 && --runaway_limit // bail if this has lost control
							 );
				}
			}
		}
		
		// TODO: would be nice to set the nearest as the first item...
		
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
		//post("moving %d to %f %f %f %d", objectId, o.pos.x, o.pos.y, o.pos.z, newhash);
		if (newhash != o.hash) {
			if (o.hash != invalidHash()) voxel_remove(mVoxels[o.hash], o);
			o.hash = newhash;
			if (newhash != invalidHash()) voxel_add(mVoxels[newhash], o);
		}
	}

	inline glm::ivec3 dim() { return glm::ivec3(mDimX, mDimY, mDimZ); }

	static uint32_t invalidObject() { return -1; }
	
	static uint32_t invalidHash() { return UINT_MAX; }
	static bool isValid(uint32_t h) { return h != invalidHash(); }
	
	inline uint32_t hash(glm::vec3 v) const {
		glm::vec4 norm = world2voxels * glm::vec4(v, 1.f);
		//return hash(norm[0]+0.5, norm[1]+0.5, norm[2]+0.5);
		return hash(norm[0], norm[1], norm[2]);
	}

	// convert x,y,z in range [0..DIM) to unsigned hash:
	// this is also the valid mVoxels index for the corresponding voxel:
	inline uint32_t hash(unsigned x, unsigned y, unsigned z) const {
		return hashx(x)+hashy(y)+hashz(z);
	}
	inline uint32_t hashx(uint32_t v) const { return v & mWrapX; }
	inline uint32_t hashy(uint32_t v) const { return (v & mWrapY)<<mShift; }
	inline uint32_t hashz(uint32_t v) const { return (v & mWrapZ)<<mShift2; }

	inline uint32_t hashx_nontoroidal(int32_t v) const {
		return (v >= 0 && v <= mWrapX) ? v 		    : invalidHash();
	}
	inline uint32_t hashy_nontoroidal(int32_t v) const {
		return (v >= 0 && v <= mWrapY) ? v<<mShift  : invalidHash();
	}
	inline uint32_t hashz_nontoroidal(int32_t v) const {
		return (v >= 0 && v <= mWrapZ) ? v<<mShift2 : invalidHash();
	}

	inline uint32_t unhashx(uint32_t h) const { return (h) & mWrapX; }
	inline uint32_t unhashy(uint32_t h) const { return (h>>mShift) & mWrapY; }
	inline uint32_t unhashz(uint32_t h) const { return (h>>mShift2) & mWrapZ; }
	
	// generate hash offset by an already generated hash:
	inline uint32_t hash(uint32_t x, uint32_t y, uint32_t z, uint32_t offset) const {
		return	hashx(unhashx(offset) + x) +
				hashy(unhashy(offset) + y) +
				hashz(unhashz(offset) + z);
	}
	
	inline uint32_t hash_nontoroidal(uint32_t x, uint32_t y, uint32_t z, uint32_t offset) const {
		// get signed version of offsets:
		int32_t ox = (((unhashx(offset)+mDimHalfX) & mWrapX) - mDimHalfX);
		int32_t oy = (((unhashy(offset)+mDimHalfY) & mWrapY) - mDimHalfY);
		int32_t oz = (((unhashz(offset)+mDimHalfZ) & mWrapZ) - mDimHalfZ);
		// apply to position & hash
		x = hashx_nontoroidal(ox + x);
		y = hashy_nontoroidal(oy + y);
		z = hashz_nontoroidal(oz + z);
		// validate & combine:
		return isValid(x) && isValid(y) && isValid(z)
		? x + y + z
		: invalidHash();

	}
	
	// this is definitely not thread-safe.
	inline Hashspace3D3& voxel_add(Voxel& v, Object& o) {
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
	inline Hashspace3D3& voxel_remove(Voxel& v, Object& o) {
		if (o.id == o.prev) {	// voxel only has 1 item
			v.first = invalidObject();
		} else {
			Object& prev = mObjects[o.prev];
			Object& next = mObjects[o.next];
			prev.next = next.id;
			next.prev = prev.id;
			// update head pointer?
			if (v.first == o.id) { v.first = next.id; }
		}
		// leave the object clean:
		o.prev = o.next = invalidObject();
		return *this;
	}
};

#endif // AL_HASHSPACE_H