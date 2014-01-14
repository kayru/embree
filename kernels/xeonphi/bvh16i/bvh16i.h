// ======================================================================== //
// Copyright 2009-2013 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#ifndef __EMBREE_BVH16I_MIC_H__
#define __EMBREE_BVH16I_MIC_H__

#include "bvh4i/bvh4i.h"

namespace embree
{
  /*! Multi BVH with 16 children. Each node stores the bounding box of
   * it's 16 children as well as a 16 child indices. */

#define BVH16_INDEX_SHIFT 7
#define BVH16_LEAF_BIT_SHIFT 5
#define BVH16_LEAF_MASK     ((unsigned int)1 << BVH16_LEAF_BIT_SHIFT)
#define BVH16_ITEMS_MASK   (BVH16_LEAF_MASK-1)
#define BVH16_OFFSET_MASK  (~(BVH16_ITEMS_MASK | BVH16_LEAF_MASK))
#define BVH16_TERMINAL_TOKEN BVH16_LEAF_MASK

  class BVH16i : public BVH4i
  {
  public:

    /*! BVH16i Node */

    struct __align(64) Node
    {
    public:

      mic_f min_x;
      mic_f max_x;
      mic_f min_y;
      mic_f max_y;
      mic_f min_z;
      mic_f max_z;
      mic_i child;
      mic_i data;

      __forceinline void set(const size_t index,const BBox3f &node)
      {
	min_x[index] = node.lower[0];
	min_y[index] = node.lower[1];
	min_z[index] = node.lower[2];

	max_x[index] = node.upper[0];
	max_y[index] = node.upper[1];
	max_z[index] = node.upper[2];

	child[index] = node.lower.a;
	data[index]  = node.upper.a;
      }

      __forceinline void set(const size_t index,const Node &node, const size_t source_index)
      {
	assert(index < 16);
	assert(source_index < 16);

	min_x[index] = node.min_x[source_index];
	min_y[index] = node.min_y[source_index];
	min_z[index] = node.min_z[source_index];

	max_x[index] = node.max_x[source_index];
	max_y[index] = node.max_y[source_index];
	max_z[index] = node.max_z[source_index];

	child[index] = node.child[source_index];
	data[index]  = node.data[source_index];
      }

      __forceinline BBox3f extract(const size_t index)
      {
	assert(index < 16);

	BBox3f node;
	node.lower[0] = min_x[index];
	node.lower[1] = min_y[index];
	node.lower[2] = min_z[index];
	node.lower.a  = child[index];

	node.upper[0] = max_x[index];
	node.upper[1] = max_y[index];
	node.upper[2] = max_z[index];
	node.upper.a  = data[index];
	return node;
      }

      __forceinline mic_f area()
      {
	const mic_f x = max_x - min_x;
	const mic_f y = max_y - min_y;
	const mic_f z = max_z - min_z;
	return (x*y+x*z+y*z) * 2.0f; 
      }

      __forceinline void shift(const size_t index)
      {
	assert(index < 16);

	for (size_t i=index+1;i<16;i++)
	  {
	    min_x[i-1] = min_x[i];
	    min_y[i-1] = min_y[i];
	    min_z[i-1] = min_z[i];

	    max_x[i-1] = max_x[i];
	    max_y[i-1] = max_y[i];
	    max_z[i-1] = max_z[i];

	    child[i-1] = child[i];
	    data[i-1]  = data[i];	
	  }
      }

      __forceinline void reset()
      {    
	min_x = mic_f(1E14);
	min_y = mic_f(1E14);
	min_z = mic_f(1E14);

	max_x = mic_f(1E14);
	max_y = mic_f(1E14);
	max_z = mic_f(1E14);

	child = mic_i(BVH16_LEAF_MASK);
	data  = mic_i(0);
      }

      __forceinline void reset(const unsigned int a,
			       const unsigned int b = 0)
      {
    
	min_x = mic_f(1E14);
	min_y = mic_f(1E14);
	min_z = mic_f(1E14);

	max_x = mic_f(1E14);
	max_y = mic_f(1E14);
	max_z = mic_f(1E14);

	child = mic_i(a);
	data  = mic_i(b);
      }

    };


  public:

    Node *node16;

    /*! BVH4 default constructor. */
    BVH16i (const PrimitiveType& primTy, void* geometry = NULL) : BVH4i(primTy,geometry)
    {
      node16 = NULL;
    }


    static Accel* BVH16iTriangle1ObjectSplitBinnedSAH(Scene* scene);

  };


    __forceinline std::ostream &operator<<(std::ostream &o, const BVH16i::Node &v)
    {
      o << "min_x " << v.min_x << std::endl;
      o << "max_x " << v.max_x << std::endl;

      o << "min_y " << v.min_y << std::endl;
      o << "max_y " << v.max_y << std::endl;

      o << "min_z " << v.min_z << std::endl;
      o << "max_z " << v.max_z << std::endl;

      o << "child " << v.child << std::endl;
      o << "data " << v.data << std::endl;

      return o;
    }

}

#endif
