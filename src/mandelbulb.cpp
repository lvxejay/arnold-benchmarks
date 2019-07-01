#include <ai.h>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace std;

// procedural parameters
struct Mandelbulb
{
   int      grid_size;       // sample grid resolution
   int      max_iterations;  // max Z^n+C iterations to try
   float    power;           // exponent, the "n" in Z^n+C
   float    sphere_scale;    // scales the spheres
   float    orbit_threshold; // clears out a hollow center in the set
   int      chunks;          // number of "chunks" for RAM management
   int      threads;         // number of threads to use
   bool     julia;           // mandelbrot/julia set switch
   AtVector julia_C;         // C value for julia sets (unused for mandelbrot)
   int counter;
};

// returns Z^n + C
// for more info: http://www.skytopia.com/project/fractal/2mandelbulb.html#formula
// this function is called often and is not very fast,
// this would be an obvious place to add optimizations,
// such as SIMD sin() functions or whatnot
static AtVector iterate(AtVector Z, float n, AtVector C)
{
   AtVector Zsquared;
   float r2 = AiV3Dot(Z, Z);
   float theta = atan2f(sqrtf(Z.x * Z.x + Z.y * Z.y), Z.z);
   float phi = atan2f(Z.y, Z.x);
   float r_n;
   if (n == 8)
      r_n = r2 * r2;
   else
      r_n = powf(r2,n*.5f);
   const float sin_thetan = sinf(theta * n);

   Zsquared.x = r_n * sin_thetan * cosf(phi * n);
   Zsquared.y = r_n * sin_thetan * sinf(phi * n);
   Zsquared.z = r_n * cosf(theta * n);
   return Zsquared + C;
}

AI_PROCEDURAL_NODE_EXPORT_METHODS(MandelbulbMtd);

node_parameters
{
   AiParameterInt("grid_size"      , 1000);
   AiParameterInt("max_iterations" , 10);
   AiParameterFlt("power"          , 8);
   AiParameterFlt("sphere_scale"   , 1);
   AiParameterFlt("orbit_threshold", 0.05);
   AiParameterInt("chunks"         , 30);
   AiParameterInt("threads"        , 4);
   AiParameterBool("julia"         , false);
   AiParameterVec("julia_C"        , 0, 0, 0);
}

// we read the UI parameters into their global vars
procedural_init
{
   Mandelbulb *bulb = new Mandelbulb();
   *user_ptr = bulb;

   bulb->grid_size = AiNodeGetInt(node, "grid_size");
   bulb->max_iterations = AiNodeGetInt(node, "max_iterations");
   bulb->power = AiNodeGetFlt(node, "power");
   bulb->sphere_scale = AiNodeGetFlt(node, "sphere_scale");
   bulb->orbit_threshold = AiSqr(AiNodeGetFlt(node, "orbit_threshold"));
   bulb->chunks = AiNodeGetInt(node, "chunks");
   bulb->threads = AiClamp(AiNodeGetInt(node, "threads"), 1, AI_MAX_THREADS);
   bulb->julia = AiNodeGetBool(node, "julia");
   bulb->julia_C = AiNodeGetVec(node, "julia_C");
   bulb->counter = 0;

   return true;
}

procedural_cleanup
{
   Mandelbulb *bulb = (Mandelbulb*)user_ptr;
   delete bulb;
   return true;
}

// we will create one node per chunk as set in the UI
procedural_num_nodes
{
   Mandelbulb *bulb = (Mandelbulb*)user_ptr;
   return bulb->chunks;
}

// this is the function that gets run on each thread
// the function (Z^n+C) is sampled at all points in a regular grid
// prisoner points with an orbit greater than bulb->orbit_threshold are added
// the total grid is broken into bulb->chnks number of slabs on the X axis
// each slab is broken into bulb->threads number of sub slabs
// and the start and end value in X is passed in and that section is sampled
void fillList(Mandelbulb *bulb, int start, int end, int chunknum, vector<AtVector>& list)
{
   float inv_grid_size = 1.0f / bulb->grid_size;
   // these vars are for a crude counter for percent completed
   unsigned int modder = static_cast<unsigned int>(float(end-start) / 5);
   int levelcount = 0;
   int percent = 0;
   int localcounter = 0;

   // only samples X in the range "start" to "end"
   for (int X = start; X < end; X++) {
      levelcount++;
      // echo out some completion info
      if (modder > 0) {
         if ((levelcount%modder) ==0 ) {
            percent += 10;
            AiMsgInfo("[mandelbulb] %d percent of chunk %d", percent, chunknum);
         }
      }

      // samples all points in Y and Z
      for (int Y = 0; Y < bulb->grid_size; Y++) {
         for (int Z = 0; Z < bulb->grid_size; Z++) {
            AtVector sample;
            sample.x = (X * inv_grid_size - 0.5f) * 2.5f;
            sample.y = (Y * inv_grid_size - 0.5f) * 2.5f;
            sample.z = (Z * inv_grid_size - 0.5f) * 2.5f;
            // init the iterator
            AtVector iterator = sample;
            // now iterate the Z^n+C function bulb->max_iterations number of times
            for (int iter = 0; iter < bulb->max_iterations; iter++) {
               if (AiV3Dot(iterator,iterator) > 4)
                  break; //orbit has left the max radius of 2....
               if (bulb->julia) {
                  // each UI value of C creates a full Julia set
                  iterator = iterate(iterator,bulb->power,bulb->julia_C);
               } else {
                  // Mandelbrot set is the centerpoints of all Julia sets
                  iterator = iterate(iterator,bulb->power,sample);
               }
            }

            // tiny orbits are usually inside the set, disallow them.
            bool allowit = AiV3Dist2(sample,iterator) >= bulb->orbit_threshold;

            // if the orbit is inside radius 2
            // and its endpoint travelled greater than orbittthresh
            if (AiV3Dot(iterator, iterator) < 4 && allowit) {
               bulb->counter++; // increment global counter
               localcounter++; // increment local counter
               // this is a prisoner point, add it to the set
               list.push_back(sample);
            }
         }
      }
   }

   AiMsgInfo("[mandelbulb] finished 1 thread of chunk %d, new total new points %d", chunknum, localcounter);
}

// this builds the "points" node in Arnold and sets
// the point locations, radius, and sets it to sphere mode
static AtNode *build_node(const AtNode *parent, Mandelbulb *bulb, vector<AtVector>& list)
{
   AtArray *pointarr = AiArrayConvert(list.size(), 1, AI_TYPE_VECTOR, &list[0]);
   vector<AtVector>().swap(list); // clear data used by points vector.
   AtNode *currentInstance = AiNode("points", "mandelbulb", parent); // initialize node as child of procedural
   AiNodeSetArray(currentInstance, "points", pointarr);
   AiNodeSetFlt(currentInstance, "radius", (2.0f/bulb->grid_size) * bulb->sphere_scale);
   AiNodeSetInt(currentInstance, "mode", 1);
   return currentInstance;
}

// a data structure to hold the arguments for the thread
// corresponds to the arguments to the fillList() function
struct ThreadArgs {
   Mandelbulb *bulb;
   int start;
   int end;
   int i;
   vector<AtVector> list;
};

// a function to be passed for the thread to execute
// basically a wrapper to the fillList() function
unsigned int threadloop(void *pointer)
{
   ThreadArgs *thread_args = (ThreadArgs*) pointer;
   fillList(thread_args->bulb, thread_args->start, thread_args->end, thread_args->i, thread_args->list);
   return 0;
}

// this is the function that Arnold calls to request the nodes
// that this procedural creates.
procedural_get_node
{
   Mandelbulb *bulb = (Mandelbulb*)user_ptr;

   // determine the start and end point of this chunk
   float chunksize = float(bulb->grid_size) / float(bulb->chunks);
   int start = static_cast<int>(i*chunksize);
   int end = static_cast<int>((i+1)*chunksize);
   if (end>bulb->grid_size)
      end = bulb->grid_size;
   float range = end - start;

   // make an array of arguments for the threads
   vector<ThreadArgs> thread_args(bulb->threads);
   vector<void*> threads(bulb->threads);

   // now loop through and launch the threads
   for (int tnum = 0; tnum < bulb->threads; tnum++) {
      // figure out the threads start and end points for the sub-chunks
      int tstart = start + static_cast<int>((range/bulb->threads)*tnum);
      int tend = start + static_cast<int>((range/bulb->threads)*(tnum+1));
      thread_args[tnum].start = tstart;
      thread_args[tnum].end = tend;
      thread_args[tnum].i = i;
      thread_args[tnum].bulb = bulb;
      threads[tnum] = AiThreadCreate(threadloop, &thread_args[tnum], 0);
   }

   // using AiThreadWait, wait 'til the threads finish
   size_t listlength = 0;
   for (int tnum = 0; tnum < bulb->threads; tnum++) {
      AiThreadWait(threads[tnum]);
      // sum up the length of all threads lists
      listlength += thread_args[tnum].list.size();
   }

   // a vector to hold all the point data
   vector<AtVector> allpoints;
   allpoints.reserve(listlength);

   // concatenate all the vectors returned by the threads
   for (int tnum = 0; tnum < bulb->threads; tnum++) {
      allpoints.insert(allpoints.end(), thread_args[tnum].list.begin(), thread_args[tnum].list.end());
      vector<AtVector>().swap(thread_args[tnum].list); // clear data
   }

   for (int k = 0; k < bulb->threads; k++)
      AiThreadClose(threads[k]);

   AiMsgInfo("[mandelbulb] total sphere count: %d", bulb->counter);

   // if it's empty, return a null and Arnold handles it well.
   // passing a node with no geometry causes errors.
   if (listlength == 0)
      return NULL;

   // build the AtNode
   return build_node(node, bulb, allpoints);
}

node_loader
{
   if (i>0)
      return false;
   node->methods      = MandelbulbMtd;
   node->output_type  = AI_TYPE_NONE;
   node->name         = "mandelbulb";
   node->node_type    = AI_NODE_SHAPE_PROCEDURAL;
   strcpy(node->version, AI_VERSION);
   return true;
}