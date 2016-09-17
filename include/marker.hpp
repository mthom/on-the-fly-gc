#ifndef MARKER_HPP_INCLUDED
#define MARKER_HPP_INCLUDED

#include <atomic>
#include <cstring>

#include "atomic_list.hpp"
#include "impl_details.hpp"
#include "large_block_list.hpp"

namespace otf_gc
{
  template <class Tracer>
  class marker
  {
  private:
    list<void*> roots;
    std::atomic<bool>& running;
    
    inline uint64_t set_color(impl_details::underlying_header_t header, color c)
    {
      using namespace impl_details;
      
      return ((header >> color_bits) << color_bits) | static_cast<underlying_header_t>(c.c);
    }

    inline typename impl_details::header_t& header(void* root)
    {
      std::ptrdiff_t pr = reinterpret_cast<std::ptrdiff_t>(root) - sizeof(impl_details::header_size);
      return *reinterpret_cast<impl_details::header_t*>(pr);
    }

    inline void clear_copy(void* buf)
    {
      if(!buf) return;
      
      header_t* hp = reinterpret_cast<header_t*>(reinterpret_cast<std::ptrdiff_t>(buf) - header_size);
      hp->~header_t();
      
      free(reinterpret_cast<void*>(hp));      
    }
    
    inline void mark_indiv(void* root, const color& c)
    {
      using namespace impl_details;
      
      header_t& header_w = header(root);
      underlying_header_t header_c = header_w.load(std::memory_order_relaxed);

      if(color(header_c & header_color_mask) != c)
      {
	size_t num_log_ptrs = Tracer::num_log_ptrs(header_c);

	auto rp = reinterpret_cast<std::size_t>(root);

	if(num_log_ptrs == 0) {
	  void* buf = Tracer::copy_obj(header_c, root);

	  if(buf)
	    roots.append(Tracer::get_derived_ptrs(header_c, buf));

	  clear_copy(buf);
	} else {		  	
	  std::size_t rp_start = rp - header_size - num_log_ptrs * log_ptr_size;	
	
	  for(std::size_t p = rp_start; p < rp - header_size; p += log_ptr_size)
	  {
	    bool dirtied = false;
	    log_ptr_t* lp = reinterpret_cast<log_ptr_t*>(p);	  
	  
	    if(lp->load() == nullptr) {
	      std::size_t obj_seg = (p - rp_start) / log_ptr_size;	    
	      void* buf = Tracer::copy_obj_segment(header_c, root, obj_seg);
	      
	      if(lp->load() == nullptr && buf)
		roots.append(Tracer::derived_ptrs_of_obj_segment(header_c, buf, obj_seg));
	      else
		dirtied = true;	    

	      clear_copy(buf);
	    } else {
	      dirtied = true;
	    }
	  
	    if(dirtied) {
	      auto lpp = lp->load();
	    
	      if(lpp) {
		list<void*> buffer_list(reinterpret_cast<typename list<void*>::node_type*>(lpp));	      	      
		auto it = buffer_list.begin();
		
		if(it != buffer_list.end() && *it)
		{
		  assert((reinterpret_cast<std::ptrdiff_t>(*it) & 1ULL) != 0ULL);
		    
		  ++it;
		
		  for(; it != buffer_list.end(); ++it) {
		    if(!(*it)) continue;
		    
		    if((reinterpret_cast<std::ptrdiff_t>(*it) & 1ULL) != 0ULL)
		      break;
		    else 
		      roots.push_front(*it);
		  }
		}
	      }
	    }
	  }
	}

	header_w.store(set_color(header_c, c), std::memory_order_relaxed);
      } else {
	assert(color(header_c & header_color_mask) == c);
      }            
    }  
  public:
    marker(list<void*>&& roots_, std::atomic<bool>& running_)
      : roots(std::move(roots_)), running(running_)
    {}
    
    inline void mark(const color& ep)
    {
      size_t ticks = 0;
      
      while(!roots.empty()) {	  	  
	void* root = roots.front();
	roots.pop_front();
	
	if(root) mark_indiv(root, ep);
	if(++ticks % impl_details::mark_tick_frequency == 0 && !running.load(std::memory_order_relaxed))
	  break;
      }      
    }
  };
}
#endif
