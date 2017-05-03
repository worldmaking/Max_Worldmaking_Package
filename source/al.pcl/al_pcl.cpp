
#include <flann/flann.hpp>

#include "al_max.h"


static t_class * max_class = 0;

class Example {
public:
	t_object ob; // max objExamplet, must be first!
	
	Example() {
		
	}
	
	~Example() {
		
	}

	void bang() {
		using namespace flann;

		int nn = 3;

		log_verbosity(0);

		/*
		Matrix<float> dataset;
		Matrix<float> query;
		//load_from_file(dataset, "dataset.hdf5", "dataset");
		//load_from_file(query, "dataset.hdf5", "query");

		Matrix<int> indices(new int[query.rows*nn], query.rows, nn);
		Matrix<float> dists(new float[query.rows*nn], query.rows, nn);

		// construct an randomized kd-tree index using 4 kd-trees
		Index<L2<float> > index(dataset, flann::KDTreeIndexParams(4));
		index.buildIndex();

		// do a knn search, using 128 checks
		index.knnSearch(query, indices, dists, nn, flann::SearchParams(128));

		//flann::save_to_file(indices, "result.hdf5", "result");

		delete[] dataset.ptr();
		delete[] query.ptr();
		delete[] indices.ptr();
		delete[] dists.ptr();

		*/
	}
};

void * example_new(t_symbol *s, long argc, t_atom *argv) {
	Example *x = NULL;
	if ((x = (Example *)object_alloc(max_class))) {
		
		x = new (x) Example();
		
		// apply attrs:
		attr_args_process(x, (short)argc, argv);
		
		// invoke any initialization after the attrs are set from here:
		
	}
	return (x);
}

void example_bang(Example *x) {
	x->bang();
}

void example_free(Example *x) {
	x->~Example();
}

void example_assist(Example *x, void *b, long m, long a, char *s)
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
	
	c = class_new("al.pcl", (method)example_new, (method)example_free, (long)sizeof(Example),
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
	class_addmethod(c, (method)example_assist,			"assist",		A_CANT, 0);


	class_addmethod(c, (method)example_bang, "bang", 0);
	
	class_register(CLASS_BOX, c);
	max_class = c;
}
