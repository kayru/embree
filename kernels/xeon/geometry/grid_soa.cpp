// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
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
 
#include "subdivpatch1cached_intersector1.h"

namespace embree
{
  namespace isa
  {  
    GridSOA::GridSOA(const SubdivPatch1Cached& patch, const SubdivMesh* const geom)
    {      
      const size_t array_elements = patch.grid_size_simd_blocks * vfloat::size;
      dynamic_large_stack_array(float,local_grid_u,array_elements+vfloat::size,64*64);
      dynamic_large_stack_array(float,local_grid_v,array_elements+vfloat::size,64*64);
      dynamic_large_stack_array(float,local_grid_x,array_elements+vfloat::size,64*64);
      dynamic_large_stack_array(float,local_grid_y,array_elements+vfloat::size,64*64);
      dynamic_large_stack_array(float,local_grid_z,array_elements+vfloat::size,64*64);

      /* compute vertex grid (+displacement) */
      evalGrid(patch,0,patch.grid_u_res-1,0,patch.grid_v_res-1,patch.grid_u_res,patch.grid_v_res,
               local_grid_x,local_grid_y,local_grid_z,local_grid_u,local_grid_v,geom);

      /* copy temporary data to tessellation cache */
      const size_t grid_offset = patch.grid_bvh_size_64b_blocks*16;
      float* const grid_x  = (float*)this + grid_offset + 0*array_elements;
      float* const grid_y  = (float*)this + grid_offset + 1*array_elements;
      float* const grid_z  = (float*)this + grid_offset + 2*array_elements;
      int  * const grid_uv = (int*  )this + grid_offset + 3*array_elements;
      assert( patch.grid_subtree_size_64b_blocks * 16 >= grid_offset + 4 * array_elements );

      /* copy points */
      memcpy(grid_x, local_grid_x, array_elements*sizeof(float));
      memcpy(grid_y, local_grid_y, array_elements*sizeof(float));
      memcpy(grid_z, local_grid_z, array_elements*sizeof(float));
      
      /* encode UVs */
      for (size_t i=0; i<array_elements; i+=vfloat::size) 
      {
        const vint iu = (vint) clamp(vfloat::load(&local_grid_u[i])*0xFFFF, vfloat(0.0f), vfloat(0xFFFF));
        const vint iv = (vint) clamp(vfloat::load(&local_grid_v[i])*0xFFFF, vfloat(0.0f), vfloat(0xFFFF));
        vint::store(&grid_uv[i], (iv << 16) | iu); 
      }
    }
      
    void* GridSOA::create(SubdivPatch1Cached* const subdiv_patch, const Scene* scene)
    {
      void* const lazymem = SharedLazyTessellationCache::sharedLazyTessellationCache.malloc(64*subdiv_patch->grid_subtree_size_64b_blocks);
      GridSOA* grid = new (lazymem) GridSOA(*subdiv_patch,scene->getSubdivMesh(subdiv_patch->geom));  
      return (void*) (size_t) grid->buildBVH(*subdiv_patch);
    }
    
    BVH4::NodeRef GridSOA::buildBVH(const SubdivPatch1Cached& patch)
    {
      BVH4::NodeRef root = 0; size_t allocator = 0;
      GridRange range(0,patch.grid_u_res-1,0,patch.grid_v_res-1);
      buildBVH(root,patch,range,allocator);
      assert(allocator == 64*patch.grid_bvh_size_64b_blocks);
      return root;
    }

    BBox3fa GridSOA::buildBVH(BVH4::NodeRef& curNode, const SubdivPatch1Cached& patch, const GridRange& range, size_t& allocator)
    {
      const size_t grid_array_elements = patch.grid_size_simd_blocks * vfloat::size;
      float* const grid_array  = (float*)this + patch.grid_bvh_size_64b_blocks * 16;

      /*! create leaf node */
      if (unlikely(range.hasLeafSize()))
      {
        const float* const grid_x_array = grid_array + 0 * grid_array_elements;
        const float* const grid_y_array = grid_array + 1 * grid_array_elements;
        const float* const grid_z_array = grid_array + 2 * grid_array_elements;
        
        /* compute the bounds just for the range! */
        BBox3fa bounds( empty );
        for (size_t v = range.v_start; v<=range.v_end; v++) 
        {
          for (size_t u = range.u_start; u<=range.u_end; u++)
          {
            const float x = grid_x_array[ v * patch.grid_u_res + u];
            const float y = grid_y_array[ v * patch.grid_u_res + u];
            const float z = grid_z_array[ v * patch.grid_u_res + u];
            bounds.extend( Vec3fa(x,y,z) );
          }
        }
        assert(is_finite(bounds));

        /* shift 2x2 quads that wrap around to the left */ // FIXME: causes intersection filter to be called multiple times for some triangles
        unsigned int u_start = range.u_start, u_end = range.u_end;
        unsigned int v_start = range.v_start, v_end = range.v_end;
        unsigned int u_size = u_end-u_start; assert(u_size > 0);
        unsigned int v_size = v_end-v_start; assert(v_size > 0);
        if (unlikely(u_size < 2 && u_start > 0)) u_start--;
        if (unlikely(v_size < 2 && v_start > 0)) v_start--;
        
        /* we store pointer to first subgrid vertex as leaf node */
        const size_t first_vertex = (size_t) &grid_x_array[v_start * patch.grid_u_res + u_start];
        const size_t base = (size_t) SharedLazyTessellationCache::sharedLazyTessellationCache.getDataPtr(); // FIXME: makes intersector dependent on tessellation cache
        const size_t value = 4*(first_vertex-base) + base;
        curNode = BVH4::encodeTypedLeaf((void*)value,2);
        return bounds;
      }
      
      /* create internal node */
      else 
      {
        /* allocate new bvh4 node */
        BVH4::Node* node = (BVH4::Node *)&((char*)this)[allocator];
        allocator += sizeof(BVH4::Node);
        node->clear();
        
        /* split range */
        GridRange r[4];
        const size_t children = range.splitIntoSubRanges(r);
      
        /* recurse into subtrees */
        BBox3fa bounds( empty );
        for (size_t i=0; i<children; i++)
        {
          BBox3fa box = buildBVH( node->child(i), patch, r[i], allocator);
          node->set(i,box);
          bounds.extend(box);
        }
        
        curNode = BVH4::encodeNode(node);
        assert(is_finite(bounds));
        return bounds;
      }
    }
  }
}